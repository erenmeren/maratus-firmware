idf_component_register(SRCS "app_main.c" "app_state.c" "render_job.c"
                       INCLUDE_DIRS "."
                       REQUIRES ui net cloud assets escpos render)
