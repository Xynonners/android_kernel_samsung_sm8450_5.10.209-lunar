import re

input_file = 'walt_kallsyms.h'

resolve_symbols = []
lines_missing_semicolon = []

macro_patterns = [
    r'KSYM_VAR\s*\(.*?,\s*(\w+)\s*\)',
    r'KSYM_VAR_RM\s*\(.*?,\s*(\w+)\s*\)',
    r'KSYM_FUNC\s*\(.*?,\s*(\w+)\s*,.*?\)',
    r'KSYM_FUNC_VOID\s*\(\s*(\w+)\s*,.*?\)'
]

with open(input_file, 'r') as f:
    lines = f.readlines()

for idx, line in enumerate(lines, 1):
    line = line.strip()

    if any(macro in line for macro in ('KSYM_VAR', 'KSYM_VAR_RM', 'KSYM_FUNC', 'KSYM_FUNC_VOID')):
        if not line.endswith(';'):
            lines_missing_semicolon.append((idx, line))

        for pattern in macro_patterns:
            match = re.match(pattern, line)
            if match:
                symbol = match.group(1)
                resolve_symbols.append(f'__resolve_{symbol},')
                break

print('Extracted __resolve_* symbols:\n')
for sym in resolve_symbols:
    print(sym)

if lines_missing_semicolon:
    print('\nLines missing semicolons:\n')
    for line_no, content in lines_missing_semicolon:
        print(f'Line {line_no}: {content}')
else:
    print('\nNo missing semicolons found.')
