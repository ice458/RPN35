#ifndef RESUME_H
#define RESUME_H

#ifdef __cplusplus
extern "C" {
#endif

// 電源OFF時に状態を保存（Resume=ON の場合のみ動作）
void resume_save_if_enabled(void);
// 起動時に復帰試行（Resume=ON かつチェックサム正常時のみ復帰）
void resume_try_restore_on_boot(void);

#ifdef __cplusplus
}
#endif

#endif
