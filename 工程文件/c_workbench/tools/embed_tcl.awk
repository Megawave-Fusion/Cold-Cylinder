# Embed UTF-8 Tcl as independent C strings (C11 only guarantees 4095 bytes/string).
BEGIN { print "/* Generated from rf_gui.tcl by tools/embed_tcl.awk. */"; print "static const char *const gui_script[] = {" }
{
    printf "\""
    for (i = 1; i <= length($0); ++i) {
        ch = substr($0, i, 1)
        if (ch == "\\" || ch == "\"") printf "\\"
        printf "%s", ch
    }
    print "\\n\","
}
END { print "NULL };" }
