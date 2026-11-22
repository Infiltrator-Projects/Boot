/* UTF-16 interactive line editor interface.
 * Cursor and viewport bounds are maintained separately from text length.
 * Results are caller-owned edited strings where documented.
 */

#ifndef __LINE_EDIT_H_
#define __LINE_EDIT_H_

BOOLEAN line_edit(CHAR16 *line_in, CHAR16 **line_out, UINTN x_max);

#endif
