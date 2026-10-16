SET(SOURCES components/ui_comp_reader.c
    components/ui_comp.c
    screens/ui_Screen1.c
    screens/ui_Screen2.c
    screens/ui_Screen3.c
    ui.c
    components/ui_comp_hook.c
    ui_helpers.c)

add_library(ui ${SOURCES})
