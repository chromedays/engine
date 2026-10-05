# Writes <target>.manifest.json next to the build output (docs/specs/page_loading.md): the build's version, a hash of the
# files the page downloads, and the byte size of each, so the page can show download progress even when the server
# compresses them and can tell when it was given files of different builds.
#   cmake -DDIR=<dir> -DTARGET=<target> -P manifest.cmake
set(entries "")
set(hashes "")
foreach(file ${TARGET}.wasm ${TARGET}.data ${TARGET}.js)
    if(EXISTS "${DIR}/${file}")
        file(SIZE "${DIR}/${file}" size)
        file(MD5 "${DIR}/${file}" hash)
        list(APPEND entries "\"${file}\": ${size}")
        string(APPEND hashes "${hash}")
    endif()
endforeach()
string(MD5 version "${hashes}")
string(SUBSTRING "${version}" 0 16 version)
list(JOIN entries ", " body)
file(WRITE "${DIR}/${TARGET}.manifest.json" "{\"version\": \"${version}\", \"files\": {${body}}}\n")
