file(SHA256 "${INPUT}" HASH)
file(WRITE "${OUTPUT}" "${HASH}  KeyboardCleaner.exe\n")
