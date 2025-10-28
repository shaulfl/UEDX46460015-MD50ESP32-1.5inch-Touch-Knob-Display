file(GLOB_RECURSE SOURCES_C *.c)
file(GLOB_RECURSE SOURCES_CPP *.cpp)

# Add UI sources explicitly
set(UI_SOURCES_C
    ui/ui.c
    ui/screens.c
    ui/styles.c
    ui/images.c
    ui/fonts/ui_font_inter_bold_26.c
    ui/fonts/ui_font_inter_bold_28.c
    ui/fonts/ui_font_inter_bold_32.c
    ui/fonts/ui_font_inter_bold_36.c
    ui/fonts/ui_font_inter_bold_52.c
    ui/fonts/ui_font_inter_bold_58.c
    ui/fonts/ui_font_inter_ebi_24.c
    ui/fonts/ui_font_inter_extrabold_italic_64.c
    ui/fonts/ui_font_inter_intealic_48.c
    ui/fonts/ui_font_inter_italic_bold_24.c
    ui/fonts/ui_font_inter_italic_bold_48.c
    ui/images/ui_image_dsd_logo_50px.c
    ui/images/ui_image_volume.c
)
list(APPEND SOURCES_C ${UI_SOURCES_C})

# Ensure the 'ui' directory is treated as a source directory so its .c files are compiled
# by the component build system. Adding "ui" to SRC_DIRS guarantees files in ui/ are built.
# Also add the LVGL headers from the managed lvgl__lvgl component so includes like
# <lvgl/lvgl.h> resolve when compiling files in main/ui/.
idf_component_register(SRC_DIRS
                    "." "ui" "ui/fonts"
                    INCLUDE_DIRS
                    "." "ui"
                    REQUIRES
                    lvgl__lvgl esp_timer espressif__esp_lcd_touch_cst816s)

# Ensure generated font .c files actually compile their font objects.
# The generated files guard the definitions with macros like UI_FONT_INTER_BOLD_58.
# Define those macros here so the font symbols (ui_font_...) are emitted.
if (NOT DEFINED FONT_GENERATED_DEFINES_ADDED)
    add_compile_definitions(
        UI_FONT_INTER_BOLD_58=1
        UI_FONT_INTER_BOLD_32=1
        UI_FONT_INTER_EXTRABOLD_ITALIC_64=1
        UI_FONT_INTER_BOLD_26=1
        UI_FONT_INTER_BOLD_52=1
    )
    set(FONT_GENERATED_DEFINES_ADDED 1 CACHE INTERNAL "Prevent adding font defines multiple times")
endif()
