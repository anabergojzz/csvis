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
| `g`, `G`, `0`, `$`                | Move to first/last column/row              |
| `i`, `a`, `c`                     | Insert/append/change text                  |
| `<Enter>`, `<Tab>`                | In insert mode proceed to next column/row  |
| `<Ctrl-C>`                        | Quit insert/visual mode, cancel            |
| `v`                               | Toogle visual mode                         |
| `V-l`, `V-j`                      | Select entire column/row                   |
| `I`, `A`, `O`, `o`                | Insert/append column/row                   |
| `d`                               | Delete selection                           |
| `D`                               | Extract if entire row/column selected      |
| `y`, `p`                          | Yank, paste                                |
| `Y`, `P`                          | Yank, paste clipboard (using xclip)        |
| `>`, `\|`, `<`                    | Pipe operations                            |
| `e`, `E`                          | Write to named pipe, write transposed      |
| `:n.m`                            | Jump to column n, row m                    |
| `s`, `S`, `<Ctrl-S>`              | Save as, save transposed as, save          |
| `u`, `<Ctrl-R>`                   | Undo, redo                                 |
| `q`                               | quit                                       |
| `<Ctrl-O>`                        | Pipe to awk                                |
| `<Ctrl-_>`                        | Read from pipe transposed                  |
| `<Ctrl-P>`                        | Paste transposed                           |
    
## Examples for piping selection
```
< seq 10                      # Insert sequence
< cat data.csv                # Insert file
| bc                          # Pipe through calculator
| tr a-z A-Z                  # Uppercase conversion
| sed 's/\./;/g'              # Replace dots with semicolons
> wc                          # Pipe to wc to count lines
e > a=[1,2,3]                 # Write to named pipe (e.g. python repl - pyrepl script)
Ctrl-O|{print $1*2}           # Pipe to awk program
```

## TODO
- [ ] option to change delimiter
- [ ] vi commands ., H, M, L, zz, zt, zb
- [ ] add commands (:q, :w)
- [ ] search
- [ ] research options for calculations
- [ ] mouse support
- [ ] multi select?
- [ ] enumerate
- [ ] how to mark chosen area (yank 5.4,2.10)
- [ ] generalise send to named pipe
- [ ] support for quotations

## Contribute
Please contact me for suggestions, advice, report issues,...
