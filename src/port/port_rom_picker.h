#ifndef PORT_ROM_PICKER_H
#define PORT_ROM_PICKER_H

#ifdef __cplusplus
extern "C" {
#endif

/* Opens the native file picker, validates the selected ROM, and installs
 * it next to the executable as baserom.gba. Returns 0 on success. */
int Port_RomPicker_PromptAndInstall(void);

#ifdef __cplusplus
}
#endif

#endif /* PORT_ROM_PICKER_H */
