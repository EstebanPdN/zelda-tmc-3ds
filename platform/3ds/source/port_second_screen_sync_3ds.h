#ifndef TMC_PORT_SECOND_SCREEN_SYNC_3DS_H
#define TMC_PORT_SECOND_SCREEN_SYNC_3DS_H

#ifdef __cplusplus
extern "C" {
#endif

void Port_SecondScreen_3DS_SyncInit(void);
void Port_SecondScreen_3DS_LockUI(void);
void Port_SecondScreen_3DS_UnlockUI(void);
void Port_SecondScreen_3DS_LockSnapshot(void);
void Port_SecondScreen_3DS_UnlockSnapshot(void);

#ifdef __cplusplus
}
#endif

#endif
