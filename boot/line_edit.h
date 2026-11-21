/*
 * The editor treats line_in as borrowed text. On Enter it returns TRUE and
 * transfers a newly allocated edited string through line_out. On cancellation
 * the caller keeps its original string. x_max specifies display columns.
 */
 





 

























#ifndef __LINE_EDIT_H_
#define __LINE_EDIT_H_

BOOLEAN line_edit(CHAR16 *line_in, CHAR16 **line_out, UINTN x_max);

#endif
