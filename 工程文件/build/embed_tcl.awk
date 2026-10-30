# Embed UTF-8 Tcl as independent C strings (C11 only guarantees 4095 bytes/string).
# Make设置LC_ALL=C，按字节扫描UTF-8；仅转义C字符串有意义的ASCII反斜杠和引号。
# 每条Tcl源行保留一个换行，末尾NULL供桥接层迭代，两个输入文件的顺序不改变。
BEGIN { print "/* Generated from workbench Tcl sources by build/embed_tcl.awk. */"; print "static const char *const gui_script[] = {" }
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
