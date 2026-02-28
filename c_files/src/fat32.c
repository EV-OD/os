/* =========================================================================
 * fat32.c – FAT12/FAT16/FAT32 Filesystem Driver
 *
 * Implements FAT filesystem parsing, cluster chain walking, and directory
 * traversal with LFN (Long File Name) support. Uses the ATA PIO block
 * driver to read sectors.
 * ========================================================================= */

#include "fat32.h"
#include "ata.h"
#include "kheap.h"
#include "log.h"
#include "string.h"

/* -------------------------------------------------------------------------
 * Internal helpers
 * ------------------------------------------------------------------------- */

static int to_lower(int c) {
    if (c >= 'A' && c <= 'Z') return c + ('a' - 'A');
    return c;
}

/**
 * Compare an 8.3 entry name with a normal string.
 * This function expects name to be up to 11 chars from the FAT dir entry.
 */
static void fat_format_83_name(const unsigned char *fat_name, const unsigned char *fat_ext, char *out) {
    int i, j = 0;
    
    for (i = 0; i < 8 && fat_name[i] != ' '; i++) {
        out[j++] = fat_name[i];
    }
    
    if (fat_ext[0] != ' ') {
        out[j++] = '.';
        for (i = 0; i < 3 && fat_ext[i] != ' '; i++) {
            out[j++] = fat_ext[i];
        }
    }
    out[j] = '\0';
}

/* =========================================================================
 * Public API Implementation
 * ========================================================================= */

int fat32_init(fat32_context_t *ctx, unsigned int partition_lba) {
    unsigned char sector_buf[512];
    
    if (!ctx) return -1;
    
    // Read the Volume Boot Record
    if (ata_read_sectors(partition_lba, 1, sector_buf) < 0) {
        log_error("[fat32] Failed to read VBR at LBA %u", partition_lba);
        return -1;
    }
    
    fat_BS_t *bpb = (fat_BS_t *)sector_buf;
    
    // Validate some basic assumptions
    if (bpb->bytes_per_sector != 512 && bpb->bytes_per_sector != 1024 &&
        bpb->bytes_per_sector != 2048 && bpb->bytes_per_sector != 4096) {
        log_error("[fat32] Invalid bytes per sector: %u", bpb->bytes_per_sector);
        return -1; // We practically only support 512 byte sectors in ATA PIO directly without scaling
    }
    
    ctx->partition_lba = partition_lba;
    ctx->bytes_per_sector = bpb->bytes_per_sector;
    ctx->sectors_per_cluster = bpb->sectors_per_cluster;
    ctx->bytes_per_cluster = ctx->bytes_per_sector * ctx->sectors_per_cluster;
    ctx->reserved_sectors = bpb->reserved_sector_count;
    ctx->fat_count = bpb->table_count;
    ctx->root_entry_count = bpb->root_entry_count;
    
    unsigned int total_sectors = (bpb->total_sectors_16 == 0) ? bpb->total_sectors_32 : bpb->total_sectors_16;
    unsigned int fat_size = (bpb->table_size_16 == 0) ? ((fat_extBS_32_t*)bpb->extended_section)->table_size_32 : bpb->table_size_16;
    
    ctx->total_sectors = total_sectors;
    ctx->fat_size = fat_size;
    
    // Compute root directory sectors
    ctx->root_dir_sectors = ((ctx->root_entry_count * 32) + (ctx->bytes_per_sector - 1)) / ctx->bytes_per_sector;
    
    // First FAT sector and First Data sector
    ctx->first_fat_sector = ctx->partition_lba + ctx->reserved_sectors;
    ctx->first_data_sector = ctx->first_fat_sector + (ctx->fat_count * ctx->fat_size) + ctx->root_dir_sectors;
    
    // Determine FAT type
    unsigned int data_sectors = ctx->total_sectors - (ctx->reserved_sectors + (ctx->fat_count * ctx->fat_size) + ctx->root_dir_sectors);
    ctx->total_clusters = data_sectors / ctx->sectors_per_cluster;
    
    if (ctx->total_clusters < 4085) {
        ctx->fat_type = FAT_TYPE_FAT12;
    } else if (ctx->total_clusters < 65525) {
        ctx->fat_type = FAT_TYPE_FAT16;
    } else {
        ctx->fat_type = FAT_TYPE_FAT32;
    }
    
    // Setup root cluster and info
    if (ctx->fat_type == FAT_TYPE_FAT32) {
        fat_extBS_32_t *ext32 = (fat_extBS_32_t *)bpb->extended_section;
        ctx->root_cluster = ext32->root_cluster;
        ctx->fat_info_sector = ext32->fat_info; // Relative to partition LBA
        
        // Read FSInfo
        if (ata_read_sectors(ctx->partition_lba + ctx->fat_info_sector, 1, sector_buf) == 0) {
            fat_fsinfo_t *fsinfo = (fat_fsinfo_t *)sector_buf;
            if (fsinfo->lead_sig == 0x41615252 && fsinfo->struc_sig == 0x61417272 && fsinfo->trail_sig == 0xAA550000) {
                ctx->free_count = fsinfo->free_count;
                ctx->next_free = fsinfo->nxt_free;
            } else {
                ctx->free_count = 0xFFFFFFFF;
                ctx->next_free = 0xFFFFFFFF;
            }
        }
    } else {
        ctx->root_cluster = 0; // Means root is in the root_dir_sectors area
        ctx->free_count = 0xFFFFFFFF;
        ctx->next_free = 0xFFFFFFFF;
    }
    
    return 0;
}

unsigned int fat32_cluster_to_lba(const fat32_context_t *ctx, unsigned int cluster) {
    if (cluster < 2) return 0; // Invalid
    return ctx->first_data_sector + ((cluster - 2) * ctx->sectors_per_cluster);
}

unsigned int fat32_next_cluster(fat32_context_t *ctx, unsigned int cluster) {
    unsigned char sector_buf[512]; // We only support 512B sectors per ATA PIO currently without alloc
    unsigned int fat_offset = 0;
    
    if (ctx->fat_type == FAT_TYPE_FAT12) {
        fat_offset = cluster + (cluster / 2);
    } else if (ctx->fat_type == FAT_TYPE_FAT16) {
        fat_offset = cluster * 2;
    } else if (ctx->fat_type == FAT_TYPE_FAT32) {
        fat_offset = cluster * 4;
    }
    
    unsigned int fat_sector = ctx->first_fat_sector + (fat_offset / ctx->bytes_per_sector);
    unsigned int ent_offset = fat_offset % ctx->bytes_per_sector;
    
    // Read the sector containing the FAT entry
    // Note: FAT12 entries can cross sector boundaries. We must read two sectors to be safe for FAT12.
    if (ctx->fat_type == FAT_TYPE_FAT12) {
        unsigned char fat_buf[1024];
        if (ata_read_sectors(fat_sector, 2, fat_buf) < 0) return 0;
        
        unsigned short table_value = *(unsigned short *)&fat_buf[ent_offset];
        if (cluster & 1) {
            table_value = table_value >> 4;
        } else {
            table_value = table_value & 0x0FFF;
        }
        
        if (table_value >= FAT12_EOC_MIN) return 0x0FFFFFF8;
        if (table_value == FAT12_BAD) return FAT32_BAD;
        return table_value;
        
    } else {
        if (ata_read_sectors(fat_sector, 1, sector_buf) < 0) return 0;
        
        if (ctx->fat_type == FAT_TYPE_FAT16) {
            unsigned short table_value = *(unsigned short *)&sector_buf[ent_offset];
            if (table_value >= FAT16_EOC_MIN) return 0x0FFFFFF8;
            if (table_value == FAT16_BAD) return FAT32_BAD;
            return table_value;
        } else { // FAT32
            unsigned int table_value = *(unsigned int *)&sector_buf[ent_offset];
            table_value &= 0x0FFFFFFF; // highest 4 bits are reserved
            
            if (table_value >= FAT32_EOC_MIN) return 0x0FFFFFF8;
            if (table_value == 0x0FFFFFF7) return FAT32_BAD;
            return table_value;
        }
    }
}

int fat32_read_cluster(fat32_context_t *ctx, unsigned int cluster, void *buf) {
    if (cluster < 2) return -1;
    unsigned int lba = fat32_cluster_to_lba(ctx, cluster);
    // Cast appropriately since ata_read_sectors expects generic byte read loops
    return ata_read_sectors(lba, ctx->sectors_per_cluster, buf);
}

int fat32_list_dir(fat32_context_t *ctx,
                   unsigned int first_cluster,
                   int (*callback)(const fat_dir_entry_t *entry, const fat_dir_loc_t *loc, const char *lfn, void *userdata),
                   void *userdata) {
    
    unsigned int block_size;
    unsigned int block_lba;
    
    int is_root_fat16 = (first_cluster == 0 && ctx->fat_type != FAT_TYPE_FAT32);
    
    if (is_root_fat16) {
        block_size = ctx->root_dir_sectors * ctx->bytes_per_sector;
        block_lba = ctx->first_fat_sector + (ctx->fat_count * ctx->fat_size);
    } else {
        block_size = ctx->bytes_per_cluster;
        block_lba = fat32_cluster_to_lba(ctx, first_cluster);
    }
    
    if (block_lba == 0) return -1;
    
    void *dir_buf = kmalloc(block_size);
    if (!dir_buf) {
        log_error("[fat32] kmalloc failed for dir block");
        return -1;
    }
    
    unsigned int cur_cluster = first_cluster;
    char lfn_buf[256];
    int lfn_len = 0;
    
    // Clear lfn_buf
    memset(lfn_buf, 0, sizeof(lfn_buf));
    
    while (1) {
        if (is_root_fat16) {
            if (ata_read_sectors(block_lba, ctx->root_dir_sectors, dir_buf) < 0) {
                kfree(dir_buf);
                return -1;
            }
        } else {
            if (fat32_read_cluster(ctx, cur_cluster, dir_buf) < 0) {
                kfree(dir_buf);
                return -1;
            }
        }
        
        fat_dir_entry_t *entries = (fat_dir_entry_t *)dir_buf;
        unsigned int entry_count = block_size / sizeof(fat_dir_entry_t);
        
        for (unsigned int i = 0; i < entry_count; i++) {
            if (entries[i].name[0] == 0x00) {
                // End of directory
                kfree(dir_buf);
                return 0;
            }
            if (entries[i].name[0] == 0xE5) {
                // Deleted entry
                lfn_len = 0;
                continue;
            }
            
            if (entries[i].attributes == FAT_ATTR_LFN) {
                fat_lfn_entry_t *lfn = (fat_lfn_entry_t *)&entries[i];
                // LFN entries are in reverse order
                int order = lfn->order & 0x1F;
                int idx = (order - 1) * 13;
                
                // Read 13 characters
                if (idx + 0 < 256) lfn_buf[idx + 0] = lfn->name1[0] & 0xFF;
                if (idx + 1 < 256) lfn_buf[idx + 1] = lfn->name1[1] & 0xFF;
                if (idx + 2 < 256) lfn_buf[idx + 2] = lfn->name1[2] & 0xFF;
                if (idx + 3 < 256) lfn_buf[idx + 3] = lfn->name1[3] & 0xFF;
                if (idx + 4 < 256) lfn_buf[idx + 4] = lfn->name1[4] & 0xFF;
                
                if (idx + 5 < 256) lfn_buf[idx + 5] = lfn->name2[0] & 0xFF;
                if (idx + 6 < 256) lfn_buf[idx + 6] = lfn->name2[1] & 0xFF;
                if (idx + 7 < 256) lfn_buf[idx + 7] = lfn->name2[2] & 0xFF;
                if (idx + 8 < 256) lfn_buf[idx + 8] = lfn->name2[3] & 0xFF;
                if (idx + 9 < 256) lfn_buf[idx + 9] = lfn->name2[4] & 0xFF;
                if (idx + 10 < 256) lfn_buf[idx + 10] = lfn->name2[5] & 0xFF;
                
                if (idx + 11 < 256) lfn_buf[idx + 11] = lfn->name3[0] & 0xFF;
                if (idx + 12 < 256) lfn_buf[idx + 12] = lfn->name3[1] & 0xFF;
                
                if (lfn->order & 0x40) { // First logical LFN entry (end of string)
                    lfn_len = idx + 13;
                    // Null terminate just in case
                    for (int n = 0; n < 13; n++) {
                        if (lfn_buf[idx + n] == 0) {
                            lfn_len = idx + n;
                            break;
                        }
                    }
                    if (lfn_len < 256) lfn_buf[lfn_len] = '\0';
                }
            } else {
                // 8.3 entry
                if (entries[i].attributes & FAT_ATTR_VOLUME_ID) {
                    continue; // Skip volume IDs
                }
                
                char short_name[13];
                fat_format_83_name(entries[i].name, entries[i].ext, short_name);
                
                const char *reported_name = (lfn_len > 0) ? lfn_buf : short_name;
                
                fat_dir_loc_t loc;
                loc.cluster = cur_cluster;
                loc.sector = block_lba + ((i * sizeof(fat_dir_entry_t)) / ctx->bytes_per_sector);
                loc.offset = (i * sizeof(fat_dir_entry_t)) % ctx->bytes_per_sector;

                int ret = callback(&entries[i], &loc, reported_name, userdata);
                if (ret != 0) {
                    kfree(dir_buf);
                    return ret;
                }
                
                // Clear LFN context for next entry
                lfn_len = 0;
                memset(lfn_buf, 0, sizeof(lfn_buf));
            }
        }
        
        if (is_root_fat16) break; // Root dir on FAT16 is contiguous, so we are done
        
        // Find next cluster
        unsigned int next = fat32_next_cluster(ctx, cur_cluster);
        if (next >= FAT32_EOC_MIN || next == 0 || next == FAT32_BAD) {
            break;
        }
        cur_cluster = next;
    }
    
    kfree(dir_buf);
    return 0;
}

// Custom structure for search
typedef struct {
    const char *target_name;
    fat_dir_entry_t *out_entry;
    fat_dir_loc_t *out_loc;
    int found;
} fat_search_ctx_t;

static int fat32_search_callback(const fat_dir_entry_t *entry, const fat_dir_loc_t *loc, const char *name, void *userdata) {
    fat_search_ctx_t *sctx = (fat_search_ctx_t *)userdata;
    
    // Case-insensitive comparison
    const char *s1 = name;
    const char *s2 = sctx->target_name;
    int match = 1;
    
    while (*s1 && *s2) {
        if (to_lower(*s1) != to_lower(*s2)) {
            match = 0;
            break;
        }
        s1++;
        s2++;
    }
    if (match && *s1 == '\0' && *s2 == '\0') {
        if (sctx->out_entry) {
            memcpy(sctx->out_entry, entry, sizeof(fat_dir_entry_t));
        }
        if (sctx->out_loc && loc) {
            memcpy(sctx->out_loc, loc, sizeof(fat_dir_loc_t));
        }
        sctx->found = 1;
        return 1; // Stop traversal
    }
    
    return 0; // Continue
}

int fat32_find_in_dir(fat32_context_t *ctx, unsigned int first_cluster, const char *name, fat_dir_entry_t *out, fat_dir_loc_t *out_loc) {
    fat_search_ctx_t sctx;
    sctx.target_name = name;
    sctx.out_entry = out;
    sctx.out_loc = out_loc;
    sctx.found = 0;
    
    fat32_list_dir(ctx, first_cluster, fat32_search_callback, &sctx);
    
    return sctx.found ? 0 : -1;
}

int fat32_find_path(fat32_context_t *ctx, const char *path, fat_dir_entry_t *out, fat_dir_loc_t *out_loc) {
    if (!path || path[0] != '/') return -1; // Must be absolute path
    
    unsigned int cur_cluster = ctx->root_cluster;
    const char *p = path + 1; // Skip initial slash
    
    // Check if looking for root
    if (*p == '\0') {
        // Create a dummy dir entry for root
        if (out) {
            memset(out, 0, sizeof(fat_dir_entry_t));
            out->attributes = FAT_ATTR_DIRECTORY;
            out->first_cluster_low = cur_cluster & 0xFFFF;
            out->first_cluster_high = (cur_cluster >> 16) & 0xFFFF;
        }
        if (out_loc) {
            out_loc->cluster = 0;
            out_loc->sector = 0;
            out_loc->offset = 0;
        }
        return 0;
    }
    
    char name_buf[256];
    
    while (*p) {
        int i = 0;
        while (*p && *p != '/' && i < 255) {
            name_buf[i++] = *p++;
        }
        name_buf[i] = '\0';
        
        while (*p == '/') p++; // Skip multiple slashes
        
        fat_dir_entry_t entry;
        fat_dir_loc_t loc;
        if (fat32_find_in_dir(ctx, cur_cluster, name_buf, &entry, &loc) < 0) {
            return -1; // Component not found
        }
        
        cur_cluster = ((unsigned int)entry.first_cluster_high << 16) | entry.first_cluster_low;
        
        if (*p == '\0') {
            // Found the final target
            if (out) memcpy(out, &entry, sizeof(fat_dir_entry_t));
            if (out_loc) memcpy(out_loc, &loc, sizeof(fat_dir_loc_t));
            return 0;
        } else {
            // Must be a directory to continue
            if (!(entry.attributes & FAT_ATTR_DIRECTORY)) {
                return -1; // Not a directory but path continues
            }
        }
    }
    
    return -1;
}

int fat32_read_file(fat32_context_t *ctx, const fat_dir_entry_t *entry, void *buf, unsigned int size) {
    if ((entry->attributes & FAT_ATTR_DIRECTORY)) return -1; // Is a directory
    
    unsigned int cur_cluster = ((unsigned int)entry->first_cluster_high << 16) | entry->first_cluster_low;
    unsigned int bytes_read = 0;
    unsigned int read_size = size < entry->file_size ? size : entry->file_size;
    char *dest = (char *)buf;
    
    void *cluster_buf = kmalloc(ctx->bytes_per_cluster);
    if (!cluster_buf) return -1;
    
    while (bytes_read < read_size && cur_cluster != 0 && cur_cluster < FAT32_EOC_MIN && cur_cluster != FAT32_BAD) {
        if (fat32_read_cluster(ctx, cur_cluster, cluster_buf) < 0) {
            kfree(cluster_buf);
            return -1;
        }
        
        unsigned int to_copy = read_size - bytes_read;
        if (to_copy > ctx->bytes_per_cluster) {
            to_copy = ctx->bytes_per_cluster;
        }
        
        memcpy(dest + bytes_read, cluster_buf, to_copy);
        bytes_read += to_copy;
        
        cur_cluster = fat32_next_cluster(ctx, cur_cluster);
    }
    
    kfree(cluster_buf);
    return bytes_read;
}

void fat32_dump_info(const fat32_context_t *ctx) {
    log_info("FAT Filesystem Info:");
    log_info("  Partition LBA      : %u", ctx->partition_lba);
    log_info("  Bytes Per Sector   : %u", ctx->bytes_per_sector);
    log_info("  Sectors Per Cluster: %u", ctx->sectors_per_cluster);
    log_info("  Total Sectors      : %u", ctx->total_sectors);
    log_info("  FAT Type           : FAT%d", ctx->fat_type);
    log_info("  FAT Size (sectors) : %u", ctx->fat_size);
    log_info("  Root Cluster       : %u", ctx->root_cluster);
    log_info("  Free Cluster Count : %u", ctx->free_count);
}

/* =========================================================================
 * Write API Implementation
 * ========================================================================= */

int fat32_write_cluster(fat32_context_t *ctx, unsigned int cluster, const void *buf) {
    if (cluster < 2) return -1;
    unsigned int lba = fat32_cluster_to_lba(ctx, cluster);
    return ata_write_sectors(lba, ctx->sectors_per_cluster, buf);
}

int fat32_set_fat_entry(fat32_context_t *ctx, unsigned int cluster, unsigned int value) {
    unsigned char sector_buf[512]; // Note: size should match bytes_per_sector. We assume 512.
    unsigned int fat_offset = 0;
    
    if (ctx->fat_type == FAT_TYPE_FAT32) {
        fat_offset = cluster * 4;
    } else if (ctx->fat_type == FAT_TYPE_FAT16) {
        fat_offset = cluster * 2;
    } else {
        return -1; // FAT12 writes not supported in this basic implementation
    }
    
    unsigned int fat_sector = ctx->first_fat_sector + (fat_offset / ctx->bytes_per_sector);
    unsigned int ent_offset = fat_offset % ctx->bytes_per_sector;
    
    // Read the FAT sector
    if (ata_read_sectors(fat_sector, 1, sector_buf) < 0) {
        return -1;
    }
    
    // Update the value
    if (ctx->fat_type == FAT_TYPE_FAT32) {
        unsigned int cur = *(unsigned int*)&sector_buf[ent_offset];
        value = (cur & 0xF0000000) | (value & 0x0FFFFFFF);
        *(unsigned int*)&sector_buf[ent_offset] = value;
    } else {
        *(unsigned short*)&sector_buf[ent_offset] = (unsigned short)value;
    }
    
    // Write the FAT sector to all FAT tables
    for (unsigned int i = 0; i < ctx->fat_count; i++) {
        unsigned int sec_to_write = fat_sector + (i * ctx->fat_size);
        if (ata_write_sectors(sec_to_write, 1, sector_buf) < 0) {
            return -1;
        }
    }
    
    return 0;
}

unsigned int fat32_alloc_cluster(fat32_context_t *ctx) {
    unsigned int start = ctx->next_free;
    if (start < 2 || start >= ctx->total_clusters) {
        start = 2;
    }
    
    // Search from next_free
    for (unsigned int i = start; i < ctx->total_clusters; i++) {
        if (fat32_next_cluster(ctx, i) == FAT32_FREE) {
            fat32_set_fat_entry(ctx, i, FAT32_EOC_MIN);
            ctx->next_free = i + 1;
            
            // clear the cluster with zeroes
            void *zero_buf = kmalloc(ctx->bytes_per_cluster);
            if (zero_buf) {
                memset(zero_buf, 0, ctx->bytes_per_cluster);
                fat32_write_cluster(ctx, i, zero_buf);
                kfree(zero_buf);
            }
            return i;
        }
    }
    
    // Second pass from start
    for (unsigned int i = 2; i < start; i++) {
        if (fat32_next_cluster(ctx, i) == FAT32_FREE) {
            fat32_set_fat_entry(ctx, i, FAT32_EOC_MIN);
            ctx->next_free = i + 1;
            
            void *zero_buf = kmalloc(ctx->bytes_per_cluster);
            if (zero_buf) {
                memset(zero_buf, 0, ctx->bytes_per_cluster);
                fat32_write_cluster(ctx, i, zero_buf);
                kfree(zero_buf);
            }
            return i;
        }
    }
    
    return 0; // Disk full
}

int fat32_update_dir_entry(fat32_context_t *ctx, const fat_dir_loc_t *loc, const fat_dir_entry_t *entry) {
    (void)ctx;
    if (!loc || loc->sector == 0) return -1;
    
    unsigned char sector_buf[512]; // Assume 512 byte sectors
    if (ata_read_sectors(loc->sector, 1, sector_buf) < 0) {
        return -1;
    }
    
    memcpy(&sector_buf[loc->offset], entry, sizeof(fat_dir_entry_t));
    
    return ata_write_sectors(loc->sector, 1, sector_buf);
}

/* =========================================================================
 * fat32_write_file – write data to an existing file
 *
 * Walks the cluster chain to find the cluster containing 'offset', then
 * writes data cluster-by-cluster, allocating new clusters as needed.
 * Updates the directory entry's file_size on disk.
 * ========================================================================= */

int fat32_write_file(fat32_context_t *ctx,
                     fat_dir_entry_t *entry,
                     const fat_dir_loc_t *loc,
                     unsigned int offset,
                     const void *data,
                     unsigned int size) {
    if (!ctx || !entry || !loc || !data || size == 0) return -1;
    if (entry->attributes & FAT_ATTR_DIRECTORY) return -1;

    unsigned int first_cluster = ((unsigned int)entry->first_cluster_high << 16) |
                                  entry->first_cluster_low;
    const char *src = (const char *)data;
    unsigned int bytes_written = 0;

    /* If the file has no cluster yet, allocate one */
    if (first_cluster < 2) {
        first_cluster = fat32_alloc_cluster(ctx);
        if (first_cluster == 0) return -1; /* disk full */
        entry->first_cluster_low  = first_cluster & 0xFFFF;
        entry->first_cluster_high = (first_cluster >> 16) & 0xFFFF;
    }

    void *cluster_buf = kmalloc(ctx->bytes_per_cluster);
    if (!cluster_buf) return -1;

    unsigned int cur_cluster = first_cluster;
    unsigned int cluster_index = 0;
    unsigned int start_cluster_idx = offset / ctx->bytes_per_cluster;

    /* Walk to the cluster containing 'offset' */
    while (cluster_index < start_cluster_idx) {
        unsigned int next = fat32_next_cluster(ctx, cur_cluster);
        if (next >= FAT32_EOC_MIN || next == 0 || next == FAT32_BAD) {
            /* Need to extend chain */
            unsigned int new_clust = fat32_alloc_cluster(ctx);
            if (new_clust == 0) { kfree(cluster_buf); return -1; }
            fat32_set_fat_entry(ctx, cur_cluster, new_clust);
            cur_cluster = new_clust;
        } else {
            cur_cluster = next;
        }
        cluster_index++;
    }

    unsigned int off_in_cluster = offset % ctx->bytes_per_cluster;

    /* Write loop */
    while (bytes_written < size) {
        /* Read current cluster contents (for partial overwrites) */
        if (fat32_read_cluster(ctx, cur_cluster, cluster_buf) < 0) {
            kfree(cluster_buf);
            return -1;
        }

        unsigned int space = ctx->bytes_per_cluster - off_in_cluster;
        unsigned int to_write = size - bytes_written;
        if (to_write > space) to_write = space;

        memcpy((char *)cluster_buf + off_in_cluster, src + bytes_written, to_write);

        if (fat32_write_cluster(ctx, cur_cluster, cluster_buf) < 0) {
            kfree(cluster_buf);
            return -1;
        }

        bytes_written += to_write;
        off_in_cluster = 0; /* subsequent clusters start at byte 0 */

        if (bytes_written < size) {
            unsigned int next = fat32_next_cluster(ctx, cur_cluster);
            if (next >= FAT32_EOC_MIN || next == 0 || next == FAT32_BAD) {
                unsigned int new_clust = fat32_alloc_cluster(ctx);
                if (new_clust == 0) { kfree(cluster_buf); return bytes_written; }
                fat32_set_fat_entry(ctx, cur_cluster, new_clust);
                cur_cluster = new_clust;
            } else {
                cur_cluster = next;
            }
        }
    }

    kfree(cluster_buf);

    /* Update file size if we extended past the old end */
    unsigned int new_end = offset + bytes_written;
    if (new_end > entry->file_size) {
        entry->file_size = new_end;
    }

    /* Write back the directory entry with updated size and cluster pointers */
    fat32_update_dir_entry(ctx, loc, entry);

    return bytes_written;
}

/* =========================================================================
 * fat32_create_file – create a new 8.3 entry in a directory
 *
 * Scans every cluster in the directory chain for the first free slot
 * (first byte 0x00 or 0xE5).  If none found, extends the directory by
 * allocating a new cluster.  Writes the new entry to disk.
 * ========================================================================= */

/* Internal: make an 8.3 name from separate name/ext strings */
static void fat32_make_83name(const char *name, const char *ext,
                              unsigned char out_name[8], unsigned char out_ext[3]) {
    int i;
    for (i = 0; i < 8; i++) {
        if (name && name[i] && name[i] != '.') {
            out_name[i] = (name[i] >= 'a' && name[i] <= 'z')
                         ? name[i] - 32 : name[i];
        } else {
            out_name[i] = ' ';
        }
    }
    for (i = 0; i < 3; i++) {
        if (ext && ext[i]) {
            out_ext[i] = (ext[i] >= 'a' && ext[i] <= 'z')
                        ? ext[i] - 32 : ext[i];
        } else {
            out_ext[i] = ' ';
        }
    }
}

int fat32_create_file(fat32_context_t *ctx,
                      unsigned int dir_cluster,
                      const char *filename,
                      const char *ext,
                      unsigned char attributes,
                      fat_dir_entry_t *out_entry,
                      fat_dir_loc_t *out_loc) {
    if (!ctx || !filename) return -1;

    void *cluster_buf = kmalloc(ctx->bytes_per_cluster);
    if (!cluster_buf) return -1;

    unsigned int cur_cluster = dir_cluster;
    unsigned int prev_cluster = 0;

    /* Walk through each cluster of the directory looking for a free entry */
    while (1) {
        if (fat32_read_cluster(ctx, cur_cluster, cluster_buf) < 0) {
            kfree(cluster_buf);
            return -1;
        }

        fat_dir_entry_t *entries = (fat_dir_entry_t *)cluster_buf;
        unsigned int count = ctx->bytes_per_cluster / sizeof(fat_dir_entry_t);
        unsigned int lba = fat32_cluster_to_lba(ctx, cur_cluster);

        for (unsigned int i = 0; i < count; i++) {
            unsigned char first = entries[i].name[0];
            if (first == 0x00 || first == 0xE5) {
                /* Found a free slot – fill it */
                memset(&entries[i], 0, sizeof(fat_dir_entry_t));
                fat32_make_83name(filename, ext, entries[i].name, entries[i].ext);
                entries[i].attributes = attributes;
                entries[i].file_size = 0;
                entries[i].first_cluster_high = 0;
                entries[i].first_cluster_low = 0;

                /* Write cluster back */
                if (fat32_write_cluster(ctx, cur_cluster, cluster_buf) < 0) {
                    kfree(cluster_buf);
                    return -1;
                }

                if (out_entry) memcpy(out_entry, &entries[i], sizeof(fat_dir_entry_t));
                if (out_loc) {
                    out_loc->cluster = cur_cluster;
                    out_loc->sector = lba + ((i * sizeof(fat_dir_entry_t)) / ctx->bytes_per_sector);
                    out_loc->offset = (i * sizeof(fat_dir_entry_t)) % ctx->bytes_per_sector;
                }

                kfree(cluster_buf);
                return 0;
            }
        }

        /* No free slot in this cluster – follow chain */
        prev_cluster = cur_cluster;
        unsigned int next = fat32_next_cluster(ctx, cur_cluster);
        if (next >= FAT32_EOC_MIN || next == 0 || next == FAT32_BAD) {
            /* Extend directory by allocating a new cluster */
            unsigned int new_clust = fat32_alloc_cluster(ctx);
            if (new_clust == 0) {
                kfree(cluster_buf);
                return -1; /* disk full */
            }
            fat32_set_fat_entry(ctx, prev_cluster, new_clust);
            cur_cluster = new_clust;
            /* The new cluster is already zeroed by alloc, so entries[0].name[0]==0 */
        } else {
            cur_cluster = next;
        }
    }
}

/* =========================================================================
 * fat32_mkdir – create a subdirectory
 *
 * Allocates a cluster, writes "." and ".." entries, then inserts an 8.3
 * directory entry into the parent.
 * ========================================================================= */

int fat32_mkdir(fat32_context_t *ctx,
                unsigned int parent_cluster,
                const char *name,
                fat_dir_entry_t *out_entry,
                fat_dir_loc_t *out_loc) {
    if (!ctx || !name) return -1;

    /* Allocate a cluster for the new directory's contents */
    unsigned int new_cluster = fat32_alloc_cluster(ctx);
    if (new_cluster == 0) return -1;

    /* Build "." and ".." entries */
    void *dir_buf = kmalloc(ctx->bytes_per_cluster);
    if (!dir_buf) return -1;
    memset(dir_buf, 0, ctx->bytes_per_cluster);

    fat_dir_entry_t *dot = (fat_dir_entry_t *)dir_buf;
    memset(dot->name, ' ', 8);
    memset(dot->ext, ' ', 3);
    dot->name[0] = '.';
    dot->attributes = FAT_ATTR_DIRECTORY;
    dot->first_cluster_low  = new_cluster & 0xFFFF;
    dot->first_cluster_high = (new_cluster >> 16) & 0xFFFF;
    dot->file_size = 0;

    fat_dir_entry_t *dotdot = (fat_dir_entry_t *)((char *)dir_buf + sizeof(fat_dir_entry_t));
    memset(dotdot->name, ' ', 8);
    memset(dotdot->ext, ' ', 3);
    dotdot->name[0] = '.';
    dotdot->name[1] = '.';
    dotdot->attributes = FAT_ATTR_DIRECTORY;
    dotdot->first_cluster_low  = parent_cluster & 0xFFFF;
    dotdot->first_cluster_high = (parent_cluster >> 16) & 0xFFFF;
    dotdot->file_size = 0;

    if (fat32_write_cluster(ctx, new_cluster, dir_buf) < 0) {
        kfree(dir_buf);
        return -1;
    }
    kfree(dir_buf);

    /* Insert directory entry into parent */
    fat_dir_entry_t created;
    fat_dir_loc_t created_loc;
    if (fat32_create_file(ctx, parent_cluster, name, (void*)0,
                          FAT_ATTR_DIRECTORY, &created, &created_loc) < 0) {
        return -1;
    }

    /* Patch the cluster pointer into the entry */
    created.first_cluster_low  = new_cluster & 0xFFFF;
    created.first_cluster_high = (new_cluster >> 16) & 0xFFFF;
    fat32_update_dir_entry(ctx, &created_loc, &created);

    if (out_entry) memcpy(out_entry, &created, sizeof(fat_dir_entry_t));
    if (out_loc)   memcpy(out_loc, &created_loc, sizeof(fat_dir_loc_t));
    return 0;
}

/* =========================================================================
 * fat32_delete_file – delete a file or directory entry
 *
 * Marks the directory entry as deleted (0xE5), then walks the cluster chain
 * freeing every cluster.
 * ========================================================================= */

int fat32_delete_file(fat32_context_t *ctx,
                      const fat_dir_entry_t *entry,
                      const fat_dir_loc_t *loc) {
    if (!ctx || !entry || !loc) return -1;

    /* Free the cluster chain */
    unsigned int cluster = ((unsigned int)entry->first_cluster_high << 16) |
                            entry->first_cluster_low;

    while (cluster >= 2 && cluster < FAT32_EOC_MIN && cluster != FAT32_BAD) {
        unsigned int next = fat32_next_cluster(ctx, cluster);
        fat32_set_fat_entry(ctx, cluster, FAT32_FREE);
        cluster = next;
    }

    /* Mark directory entry as deleted */
    fat_dir_entry_t deleted;
    memcpy(&deleted, entry, sizeof(fat_dir_entry_t));
    deleted.name[0] = 0xE5;
    return fat32_update_dir_entry(ctx, loc, &deleted);
}
