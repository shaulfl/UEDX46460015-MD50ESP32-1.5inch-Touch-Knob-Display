file(GLOB_RECURSE SOURCES_C *.c)
file(GLOB_RECURSE SOURCES_CPP *.cpp)

idf_component_register(SRC_DIRS
                    "." 
                    "ui/components"
                    "ui/images"
                    "ui/screens"
                    "ui"
                    INCLUDE_DIRS
                    ".")

