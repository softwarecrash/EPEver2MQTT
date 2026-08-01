"""Check embedded architecture rules that are easy to regress."""

from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "src"
EXCLUDED = {SOURCE / "html.h"}


def source_files():
    for pattern in ("*.h", "*.cpp"):
        for path in SOURCE.rglob(pattern):
            if path not in EXCLUDED:
                yield path


def find(pattern: str):
    expression = re.compile(pattern)
    matches = []
    for path in source_files():
        for line_number, line in enumerate(
            path.read_text(encoding="utf-8").splitlines(), 1
        ):
            if expression.search(line):
                matches.append(
                    f"{path.relative_to(ROOT)}:{line_number}: {line.strip()}"
                )
    return matches


checks = {
    "Modbus UART must not be used for diagnostics": (
        r"\b(?:Serial|EPEVER_SERIAL)\s*\.\s*(?:print|println|printf)\s*\("
    ),
    "ArduinoJson ABI flags belong in platformio.ini only": (
        r"\bARDUINOJSON_USE_(?:DOUBLE|LONG_LONG)\b"
    ),
    "millis comparisons must be overflow-safe": (
        r"\bmillis\s*\(\s*\)\s*(?:>|>=)\s*[^;\n]+\+"
    ),
    "Register addresses belong in the register maps": (
        r"\b(?:readInputRegisters|readHoldingRegisters|readCoils|"
        r"writeSingleRegister|writeMultipleRegisters|writeSingleCoil)"
        r"\s*\(\s*0x"
    ),
    "Avoid indirect std::function control flow": r"\bstd::function\b",
}

failed = False
for description, pattern in checks.items():
    matches = find(pattern)
    if not matches:
        continue
    failed = True
    print(f"FAIL: {description}")
    for match in matches:
        print(f"  {match}")

if failed:
    sys.exit(1)

print("Architecture invariant checks passed.")
