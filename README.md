# csvis - visualise and edit CSV files

Simple terminal program to quickly visualise CSV files in tabular form.  
Uses vi-style motions and modal editing (Normal/Visual/Insert modes).

For additional functionality you can pipe selected data to/from/through
external programs, for example to sort data, insert sequence of numbers or
do more complex calculations using awk.

## Build
```sh
cd makefile_folder
make
```

## Keybindings
| Command                           | Action                                     |
|-----------------------------------|--------------------------------------------|
| `h`, `j`, `k`, `l`, `arrow keys`  | Move one column/row                        |
| `<Ctrl-D>`, `<Ctrl-U>`, `w`, `b`  | Move multiple columns/rows                 |
| `gg`, `G`, `0`, `$`               | Move to first/last column/row              |
| `i`, `a`, `c`                     | Insert/append/change text                  |
| `<Enter>`, `<Tab>`                | In insert mode proceed to next column/row  |
| `<Ctrl-C>`                        | Quit insert/visual mode, cancel            |
| `v`                               | Toogle visual mode                         |
| `Vl`, `Vj`, `V$`,...              | Select entire column/row                   |
| `I`, `A`, `O`, `o`                | Insert/append column/row                   |
| `d`                               | Delete(wipe) selection                     |
| `D`, `Dl`, `D$`,...               | Extract selected rows/columns              |
| `y`, `p`                          | Yank, paste                                |
| `<Ctrl-Y>`, `<Ctrl-P>`            | Yank, paste clipboard (using xclip)        |
| `>`, `\|`, `<`                    | Pipe operations                            |
| `e`                               | Write to named pipe                        |
| `:n.m`                            | Jump to column n, row m                    |
| `s`, `<Ctrl-S>`                   | Save as, save                              |
| `u`, `<Ctrl-R>`                   | Undo, redo                                 |
| `q`                               | Quit                                       |
| `<Ctrl-O>`                        | Pipe to awk                                |
| `r`                               | Inverse operation (paste, save, pipe)      |
| `/`, `?`                          | Search forward, backward                   |
| `g/`, `g?`                        | Search in selection forward, backward      |
| `n`, `N`                          | Next, previous match                       |
| `zt`, `zz`, `zb`                  | Move screen                                |
| `gc`                              | Evaluate equations (=$y.x)                 |
    
## Examples for piping selection
```
< seq 10                      # Insert sequence
< cat data.csv                # Insert file
| bc                          # Pipe through calculator
| sort -u                     # Sort unique cells
| tr a-z A-Z                  # Uppercase conversion
| sed 's/\./;/g'              # Replace dots with semicolons
> wc                          # Pipe to wc to count lines
e > a=[1,2,3]                 # Write to named pipe (e.g. python repl - pyrepl script)
```
When using pipe commands, start typing and then use `<Up>` and `<Down>` to choose predetermined command and select it with `<Tab>`.

Equations are fields that start with `=` and are evaluated in adjacent cell. Cells are referenced with string of shape `$y.x`.

## TODO
- [ ] mouse support
- [ ] multi select?
- [ ] enumerate

## Contribute
Please contact me for suggestions, advice, report issues,...
