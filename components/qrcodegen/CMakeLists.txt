# Header-only: the qrcodegen_* implementation is provided by LVGL's bundled copy
# (lv_qrcode uses the same Nayuki qrcodegen). We expose only the header so render.c
# can call those symbols without a duplicate-definition link conflict.
idf_component_register(INCLUDE_DIRS "include")
