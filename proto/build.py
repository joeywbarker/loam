# Inline core.js into shell.html -> index.html (single-file page)
core = open('core.js').read().replace('if (typeof module !== "undefined") module.exports = Core;', '')
open('index.html', 'w').write(open('shell.html').read().replace('/*CORE*/', core))
