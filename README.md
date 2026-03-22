Todo list

- [x] Copy/Cut and Paste (Cancelable) (y/x for yank/cut, Y/X for cancel, p for paste, P for paste with overwriting)
- [x] Copy Path(relative or absolute) to System Clipboard. (`cc` for copy entry, `cd` for copy current directory, `cC` for copy entry with absolute path, `cD` for copy current directory with absolute path, `cf` for copy file name, `cn` for copy name without extension)
- [x] Add support to simlink rendering. (And safely indicate the recursive link problem)
- [x] Changeable Sort order. (`,m` by modified time, `,b` by birth time, `,e` by extension, `,a` by alphabetically, `,n` sort naturally, `,s` by size. And with upper letter will sort reversely.)
- [ ] Support toml configuration, and load all configurable properties (like icons, theme, layout) from it instead of hard-coded. At the same time, support user extension.
- [ ] Multiple selection (by Visual Mode)
- [ ] Better Error handling. (Maybe by Notification Toast)
- [ ] Notification toast (Message, Warning, Error etc.)

Bug Report
- [x] program will crash if simlink link itself.
- [x] The interface does not move when using the up and down navigation. (Correct functionality: When the cursor is at 25% of the screen, moving it upwards will shift the list downwards (if there are enough elements above); when the cursor is at 75% of the screen, moving it downwards will shift the list upwards (if there are enough elements).)