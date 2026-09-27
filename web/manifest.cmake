# Writes <target>.manifest.json next to the build output: the byte size of each file the page
# downloads, so it can show download progress even when the server compresses them.
#   cmake -DDIR=<dir> -DTARGET=<target> -P manifest.cmake
set(entries "")
foreach(file ${TARGET}.wasm ${TARGET}.data)
    if(EXISTS "${DIR}/${file}")
        file(SIZE "${DIR}/${file}" size)
        list(APPEND entries "\"${file}\": ${size}")
    endif()
endforeach()
list(JOIN entries ", " body)
file(WRITE "${DIR}/${TARGET}.manifest.json" "{${body}}\n")
