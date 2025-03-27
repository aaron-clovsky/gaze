# gaze - Another scrollable watch command

## MOTIVATION
[watchall](https://github.com/mfatemipour/watchall) is a fanastic tool
for observing the output of commands in realtime that print more than one
screen's worth of data, it is also out of date and requires manual patching
after installing with pip. It also requires manually setting the size
of the line buffer, both of which makes it incredibly annoying to use. After
years of being annoyed by watchall throwing exceptions on long input I finally
decided to fix the problem the hard way.

## CREDIT

- [Mohammad Fatemipopur](https://github.com/mfatemipour) for making watchall
- [Thomas E. Dickey](https://github.com/ThomasDickey) for writing padview.c,
which gaze is based on

## TODO

- Cleanup ```parse_args()``` because it is ugly
- Add diff support
- Maybe add support for intervals of millisecond precision

## NOTES

- The clang_format file is meant to be used with clang-format-21

## LICENSE
This software is licensed under the
[MIT License](https://opensource.org/licenses/MIT).
