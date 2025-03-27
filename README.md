# gaze - Another scrollable watch command

## MOTIVATION
[watchall](https://github.com/mfatemipour/watchall) is a fanastic tool
for observing the output of commands that print more than one screen's worth
of data. It is also out of date, requiring manual patching
after installation as well as manually setting the length
of the line buffer for long inputs, both of which make it incredibly annoying
to use. After years of being annoyed by watchall throwing exceptions
I finally decided to fix the problem the right way.

## CREDIT

- [Mohammad Fatemipopur](https://github.com/mfatemipour) for making watchall
- [Thomas E. Dickey](https://github.com/ThomasDickey) for writing padview.c,
which gaze is based on

## INSTALL

- Run ```sudo make install```

## UNINSTALL

- Run ```sudo make uninstall```

## TODO

- Cleanup ```parse_args()``` because it is ugly
- Add diff support
- Add search support
- Maybe add color support
- Maybe add support for intervals of millisecond precision

## NOTES

- ```make style``` is implemented to keep code within style guidelines, 
it requires clang-format-21, run the following commands to install:
```
wget https://apt.llvm.org/llvm.sh
chmod +x llvm.sh
sudo ./llvm.sh 21
sudo apt install clang-format-21
```

## LICENSE
This software is licensed under the
[MIT License](https://opensource.org/licenses/MIT).
