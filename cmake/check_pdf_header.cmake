# PDF smoke check: verify the file exists, has a plausible size, carries the
# "%PDF-" magic, mentions the catalog and pages objects we expect, and ends
# with "%%EOF". This is still just a sanity check — it does not parse the
# xref table or validate object streams — but it catches the overwhelming
# majority of "the writer silently corrupted something" regressions that a
# header-only check would miss.
if(NOT DEFINED PDF)
    message(FATAL_ERROR "check_pdf_header.cmake requires -DPDF=<path>")
endif()

if(NOT EXISTS "${PDF}")
    message(FATAL_ERROR "PDF not produced: ${PDF}")
endif()

file(SIZE "${PDF}" _pdf_size)
if(_pdf_size LESS 256)
    message(FATAL_ERROR "PDF suspiciously small (${_pdf_size} bytes): ${PDF}")
endif()

file(READ "${PDF}" _pdf_head LIMIT 5 HEX)
# "%PDF-" in hex.
if(NOT _pdf_head STREQUAL "25504446 2d" AND NOT _pdf_head STREQUAL "255044462d")
    message(FATAL_ERROR "PDF header mismatch for ${PDF}: got hex '${_pdf_head}'")
endif()

# Read the whole file as hex so compressed (binary) stream objects don't
# truncate the in-memory string at a NUL byte. The markers we care about
# are all plain ASCII and live outside compressed streams.
file(READ "${PDF}" _pdf_hex HEX)
# "/Type /Catalog" in lowercase hex.
string(FIND "${_pdf_hex}" "2f54797065202f436174616c6f67" _has_catalog)
if(_has_catalog EQUAL -1)
    message(FATAL_ERROR "PDF missing /Type /Catalog: ${PDF}")
endif()
# "/Type /Pages"
string(FIND "${_pdf_hex}" "2f54797065202f5061676573" _has_pages)
if(_has_pages EQUAL -1)
    message(FATAL_ERROR "PDF missing /Type /Pages: ${PDF}")
endif()
# "%%EOF"
string(FIND "${_pdf_hex}" "2525454f46" _has_eof)
if(_has_eof EQUAL -1)
    message(FATAL_ERROR "PDF missing %%EOF trailer: ${PDF}")
endif()

message(STATUS "OK: ${PDF} (${_pdf_size} bytes)")
