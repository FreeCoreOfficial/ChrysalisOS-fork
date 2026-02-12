#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Entry point pentru mediul grafic (comanda 'launch' sau 'win') */
int cmd_launch(int argc, char **argv);
int cmd_launch_exit(int argc, char **argv);
int cmd_logoff(int argc, char **argv);
bool win_is_gui_running(void);

#ifdef __cplusplus
}
#endif
