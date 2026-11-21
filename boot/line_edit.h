/*
 * Bounded interactive editor for EFI loader options; edits affect the next
 * launch command line, not the on-disk executable or a persistent Boot#### path.
 */
 





 

























#ifndef __LINE_EDIT_H_
#define __LINE_EDIT_H_

BOOLEAN line_edit(CHAR16 *line_in, CHAR16 **line_out, UINTN x_max);

#endif
