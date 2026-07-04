set(UI_DIR ${CMAKE_CURRENT_LIST_DIR})

file(GLOB UI_FONT_SRCS ${UI_DIR}/assets/fonts/*.c)
file(GLOB UI_ICON_SRCS ${UI_DIR}/assets/icons/*.c)

idf_component_register(
    SRCS
        "tempchart_ui.c"
        ${UI_FONT_SRCS}
        ${UI_ICON_SRCS}
    INCLUDE_DIRS "."
    REQUIRES lvgl
)
