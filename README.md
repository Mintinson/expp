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
    - [ ] The style is rather ugly and inconsistent with the original theme. It will be improved to be more aesthetically pleasing and have more consistent colors in the future.
    - [x] Currently, the help menu interface moves with the cursor, but it stops at the end of the menu, making it impossible to see the shortcut keys at the end.
    - [ ] Sometimes program will crash when filter something into filter mode.
- [x] After the refactoring of `ExplorerView`, deleting multiple files and adding them to the recycle bin in visual mode are no longer supported; only the file currently pointed to by the cursor can be deleted.


Fix Report

### Currently, the help menu interface moves with the cursor, but it stops at the end of the menu, making it impossible to see the shortcut keys at the end.


**Defect A**: Incorrect calculation of the max_offset upper limit.

In the `clamp_help_viewport` function, the upper limit of the scroll offset is strictly limited:

```c++
const int max_offset = std::max(0, static_cast<int> (entry_count) - viewport.viewportRows);
// ...
viewport.scrollOffset = std::clamp(viewport.scrollOffset, 0, max_offset);

```

This formula assumes each entry occupies only 1 line of height. If the visible area is 20 lines and there are a total of 61 options, it will assume that the maximum scrollOffset can only be 41 (i.e., 61-20).
What actually happened: In the `render` function, category switching inserts `separatorLight()`, which consumes extra rows (cost = 2). When you scroll down, `scrollOffset` gets stuck at 41. The `render` function starts rendering from 41, but because several category separators consume row quota (rows_remaining), the rendering loop uses up its 20-row height quota by the time it finishes drawing the 56 th option. The remaining options 57-61 are never pushed into the body for rendering.

**Defect B**: Margin checks based on "index" rather than "visual height".

In `clamp_help_viewport`, all scroll trigger conditions are based on a pure array of indexes:

```c++
if (viewport.selectedIndex >= viewport.scrollOffset + bottom_margin) { ... }
if (viewport.selectedIndex >= viewport.scrollOffset + viewport.viewportRows) { ... }
```

When selecting select option 57, its index difference from scrollOffset(41) is 16. Because 16 is less than viewportRows (let's say 20), the forced scrolling mechanism assumes that "item 57 is on screen and doesn't need to scroll down further." However, it doesn't realize that due to the presence of the dividing line, item 57 has been visually pushed off-screen.

This leads to the phenomenon that when comes to the last part of the help menu, the UI no longer scrolls down, but the selected index continues to increase, and eventually the highlighted cursor disappears at the bottom of the screen.

**Defect C**: error in calculating the windows height.

At the end of your render function, there is a line of code that determines the height of the entire pane:

```c++
size(HEIGHT, EQUAL, body_rows + kChromeRows)
```

This will lead to the shrink of the help menu when comes to the bottom of the help menu.
Why does it shrink? When scrolling to the bottom of the list, the remaining options are not enough to fill the entire viewport (for example, the viewport can hold 20 rows, but only 16 rows are left). At this point, body_rows becomes 16, causing the pane to shrink in physical size.

Why are some options missing? FTXUI uses dynamic layout. When your pane shrinks, the viewport.viewportRows passed in from the parent container shrinks when the next frame is re-rendered! This causes clamp_help_viewport to mistakenly think the screen has become shorter, prematurely truncating the rendering and thus "squeezing" the last few [e, m, n] off the screen.