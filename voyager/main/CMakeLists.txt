set(srcs "voyager_main.c"
         "voyager_ble/voyager_ble.c"
         "characteristics/voyager_primary.c")
idf_component_register(SRCS "${srcs}"
                    INCLUDE_DIRS ".")
