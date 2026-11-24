"""Turn the listener's phone page (HTML) into a C header served from flash."""
import sys
src, dst = sys.argv[1], sys.argv[2]
html = open(src, encoding="utf-8").read()
assert ')GHOSTPAGE"' not in html
with open(dst, "w", encoding="utf-8") as f:
    f.write("// Generated from page.html by tools/embed_page.py. Do not edit.\n#pragma once\n")
    f.write('static const char PAGE_HTML[] PROGMEM = R"GHOSTPAGE(' + html + ')GHOSTPAGE";\n')
