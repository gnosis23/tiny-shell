tiny-shell
==========
## run by
```bash
make
./tsh
```

## builtin-command
```bash
tsh> quit
tsh> jobs
tsh> bg
tsh> fg
tsh> pwd
tsh> cd <directory>
tsh> environ
```

## other command 
```bash
tsh> echo hello world
tsh> ls -l
# simple pipe
tsh> ls > y
tsh> cat < y | sort | uniq | wc > y1
tsh> cat y1
```

## features to be added
- [ok]redirections
- [ok]pipe line
- [ok]cd 
- [ok]remove the command path prefix
- shell script


