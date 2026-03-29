Todo list

- [x] Copy/Cut and Paste (Cancelable) (y/x for yank/cut, Y/X for cancel, p for paste, P for paste with overwriting)
- [x] Copy Path(relative or absolute) to System Clipboard. (`cc` for copy entry, `cd` for copy current directory, `cC` for copy entry with absolute path, `cD` for copy current directory with absolute path, `cf` for copy file name, `cn` for copy name without extension)
- [x] Add support to simlink rendering. (And safely indicate the recursive link problem)
- [x] Changeable Sort order. (`,m` by modified time, `,b` by birth time, `,e` by extension, `,a` by alphabetically, `,n` sort naturally, `,s` by size. And with upper letter will sort reversely.)
- [x] Multiple selection by vim-like visual mode, and support like multiple copy, cut, trash, delete, etc. 
- [x] Support toml configuration, and load all configurable properties (like icons, theme, layout) from it instead of hard-coded. At the same time, support user extension.
- [x] Notification toast (Message, Warning, Error etc.)
- [x] Better Error handling. Maybe by Notification Toast when there is a slight error, but stop the program when there is a serious error. The termination of the program should be safe and do necessary clean up.
- [x] Implement directory navigation functionality. `gh` navigates to the home directory, `gc` navigates to the expp configuration directory, `gl` enters the directory of the link currently pointed to by the cursor (only effective if the current target is a link), and `g:` enters the input box where any directory can be entered. An error message will be displayed if the directory does not exist.
- [x] `~` to open Help menu. Displays all keyboard shortcuts and their corresponding descriptions. Shortcuts are grouped by category, and you can navigate up and down using the `j`,`k`. You can also enter filter mode using the `f` key. The filters can be applied to both keyboard shortcuts and their descriptions.

Bug Report
- [x] program will crash if simlink link itself.
- [x] The interface does not move when using the up and down navigation. (Correct functionality: When the cursor is at 25% of the screen, moving it upwards will shift the list downwards (if there are enough elements above); when the cursor is at 75% of the screen, moving it downwards will shift the list upwards (if there are enough elements).)
- [x] Regarding the priority of importing user configuration and default configuration, during the initialization of key_handler.cpp, the user configuration is attempted to be imported first. Only if the import fails is the default configuration imported. This is because the import function returns `expected`.<void, error> Even when the `keys` field is missing, it returns `{}`, which will be evaluated as true in `result.has_value()`. Therefore, it skips the default binding, resulting in no keys being bound. The solution is to bind the default key first, then attempt to bind the user-defined key. (If duplicates occur, the user-defined key will override the default.)
- [ ] The help menu has a few issues:
    1. The style is rather ugly and inconsistent with the original theme. It will be improved to be more aesthetically pleasing and have more consistent colors in the future.
    2. Currently, the help menu interface moves with the cursor, but it stops at the end of the menu, making it impossible to see the shortcut keys at the end.