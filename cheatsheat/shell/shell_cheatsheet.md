# rosh — Shell Quick Reference

## Prompt

```
rabin@RandomOS:/current/dir $
```

---

## Command Resolution Order

```
1. starts with ./ or /  →  run literal .rox path
2. ends with .rox        →  resolve from cwd
3. /bin/<cmd>.rox        →  load external executable
4. built-in table        →  run built-in handler
5. error: command not found
```

---

## Filesystem Commands

| Command | Example | Description |
|---------|---------|-------------|
| `ls` | `ls -a /home` | List (with `-a`: show hidden) |
| `cd` | `cd /bin` | Change directory |
| `pwd` | `pwd` | Print working directory |
| `cat` | `cat /etc/motd` | Print file |
| `mkdir` | `mkdir /home/projects` | Create directory |
| `touch` | `touch /home/notes.txt` | Create empty file |
| `rm` | `rm /tmp/old.ros` | Remove file |
| `write` | `write /etc/motd Hello` | Overwrite file with text |
| `stat` | `stat /bin/rxt.rox` | Show file metadata |

---

## System Commands

| Command | Example | Description |
|---------|---------|-------------|
| `help` | `help` | List all commands |
| `clear` | `clear` | Clear screen |
| `echo` | `echo hi world` | Print text |
| `uname` | `uname` | OS info |
| `whoami` | `whoami` | Current user |
| `mode` | `mode gui` | Show/set mode (`nerd`/`gui`) |

---

## Compiler & Tools

| Command | Example | Description |
|---------|---------|-------------|
| `rosc` | `rosc hello.ros` | Compile `.ros` → `.rox` |
| `rosc -f` | `rosc -f hello.ros out.rox` | Force overwrite output |
| `sample list` | `sample list` | Show available templates |
| `sample` | `sample funcs` | Generate `funcs.ros` in cwd |
| `setup` | `setup` | Install `rxt` and `term` to `/bin/` |

---

## Running Programs

```
./hello.rox             # relative path (in cwd)
/bin/rxt.rox            # absolute path
rxt myfile.txt          # bare name → /bin/rxt.rox
```

---

## Desktop (GUI mode)

```
desktop add myapp           # add /bin/myapp.rox as an icon
desktop add ./prog.rox      # add icon from literal path
desktop delete myapp        # remove icon by label
microui                     # launch microui widget demo
```

---

## Filesystem Layout

```
/bin/          .rox executables
/etc/          hostname, motd, desktop.conf
/home/         user files
/tmp/          temporary / staging
/var/log/      log files
/lib/ros/      gui.ros  math.ros  io.ros
```

---

## Path Rules

- Absolute: `/foo/bar`
- Relative: `bar` → joined to cwd
- Trailing `/` removed (except root)

---

## Typical Workflow

```
sample hello              # create hello.ros
cat hello.ros             # inspect
rosc hello.ros            # compile → hello.rox
./hello.rox               # run
desktop add ./hello.rox   # add to desktop (GUI mode)
```
