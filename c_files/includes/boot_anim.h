#ifndef BOOT_ANIM_H
#define BOOT_ANIM_H

/* =========================================================================
 * boot_anim.h – Text-based boot loading animation
 *
 * Displays an ASCII art banner along with a progress bar while kernel
 * subsystems initialise.  Purely cosmetic – the animation is driven by
 * PIT ticks to avoid depending on the scheduler (which isn't running
 * during early boot).
 * ========================================================================= */

/**
 * boot_animation – display the full boot loading sequence.
 *
 * 1. Clear screen
 * 2. Show ASCII art OS logo
 * 3. Animate a progress bar from 0% to 100%
 * 4. Pause briefly, then clear screen for shell
 *
 * This is a blocking call.  It uses pit_get_ticks() for timing.
 */
void boot_animation(void);

#endif /* BOOT_ANIM_H */
