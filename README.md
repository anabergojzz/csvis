# Csvis - visualise and edit csv files

Simple terminal program to quickly visualise csv files in tabular form.
It uses vi motions and takes advantage of modal editing (normal, visual, insert).
It is inspired by text editor Vis.
For additional functionality you can pipe selected fields to/from/through external programs.
It is in experimental and "learn how to code" state.

## Build

run make in makefile folder

## Documentation

- For all commands see beginning of main.c file.
- Usual vi letters for navigation
- I,A,o,O for inserting columns/rows
- V-l, V-j for selecting entire column/row
- >,|,< for pipes, e for named pipe (for pipe to repl with memory - for example pyrepl)
- :n.m move to n-th column and m-th row.
    
## Examples for piping selection
    | bc
    | tr az-AZ
    | sed s/\./;/g
    Ctrl-O|{a=$1+$2; print $0,a}
    > wc
    e > a=[1,2,3]
    < seq 10
    < cat data.csv
    draw diagram with matplotlib

## Bugs
    - error when yank -> undo -> paste
    - errors when inserting wide characters at the end of line
    - inserting text when screen is full

## TODO
    - Change delimiter
    - leave file structure (without adding separators) if changing only fields that exist (!= NULL)
    - vi movements page down, up, H, M, L, zz, zt, zb
    - add :q, :w
    - struct for display state (scr_x, v_y, y)
    - search (grep?)
    - how to calculate sum?
    - mouse support
    - multi select?
    - show full width of cells
    - enumerate
    - "." to repeat
    - how to write chosen area (yank 5-4,2-10)
    - generalise send to named pipe
