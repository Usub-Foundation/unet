cmake_minimum_required(VERSION 3.22)

# ---------------------------------------------------------------------
# TLS libraries — whichever toolset is present.
# ---------------------------------------------------------------------
if(WIN32)
    set(_UNET_EXAMPLE_TLS_LIBS crypt32 secur32)
else()
    find_package(OpenSSL REQUIRED)
    set(_UNET_EXAMPLE_TLS_LIBS OpenSSL::SSL OpenSSL::Crypto)
endif()

# ---------------------------------------------------------------------
# Runtime artifacts generated next to the example binaries so a run from
# ${CMAKE_CURRENT_BINARY_DIR} (i.e. build/examples/) finds them as
# ./cert.pem, ./key.pem, ./cert.pin, ./unet_demo_file.txt.
#
# Each target below depends on unet_examples_runtime_artifacts, so a
# single build produces the binary plus everything it opens at runtime.
# Regeneration is skipped if the OUTPUTs are newer than the DEPENDS.
# ---------------------------------------------------------------------
set(_UNET_CERT_PEM ${CMAKE_CURRENT_BINARY_DIR}/cert.pem)
set(_UNET_KEY_PEM  ${CMAKE_CURRENT_BINARY_DIR}/key.pem)
set(_UNET_CERT_PFX ${CMAKE_CURRENT_BINARY_DIR}/server.pfx)
set(_UNET_CERT_PIN ${CMAKE_CURRENT_BINARY_DIR}/cert.pin)
set(_UNET_DEMO_FILE ${CMAKE_CURRENT_BINARY_DIR}/unet_demo_file.txt)

# Static demo file — cheapest to write at configure time. Any change to the
# content shows up on next configure, no rebuild dance needed.
file(WRITE ${_UNET_DEMO_FILE}
    "this file is served via res.file(fd, len)\n"
)

if(WIN32)
    # ------------------------------------------------------------------
    # Windows / SChannel path — powershell New-SelfSignedCertificate, then
    # export as PFX. Pin (hex SHA-256 of the DER-encoded leaf cert) computed
    # inline from the same certificate object so pfx and pin cannot drift.
    # ------------------------------------------------------------------
    add_custom_command(
        OUTPUT  ${_UNET_CERT_PFX} ${_UNET_CERT_PIN}
        COMMAND powershell -NoProfile -ExecutionPolicy Bypass -Command
            "$c = New-SelfSignedCertificate -DnsName localhost,127.0.0.1 -CertStoreLocation cert:\\CurrentUser\\My -KeyUsage DigitalSignature,KeyEncipherment -NotAfter (Get-Date).AddYears(1);"
            "$p = ConvertTo-SecureString -String 'demo' -Force -AsPlainText;"
            "Export-PfxCertificate -Cert $c -FilePath '${_UNET_CERT_PFX}' -Password $p | Out-Null;"
            "$hash = [System.Security.Cryptography.SHA256]::Create().ComputeHash($c.RawData);"
            "[System.IO.File]::WriteAllText('${_UNET_CERT_PIN}', ([BitConverter]::ToString($hash) -replace '-','').ToLower());"
            "Remove-Item ('cert:\\CurrentUser\\My\\' + $c.Thumbprint)"
        COMMENT "[unet examples] generating server.pfx + cert.pin (SChannel)"
        VERBATIM
    )
else()
    # ------------------------------------------------------------------
    # POSIX / OpenSSL path — cert.pem + key.pem from `openssl req`, pin
    # computed by piping DER through `openssl dgst`. The pin file has no
    # trailing whitespace so a tiny C++ read like `std::ifstream >> pin`
    # captures the whole hex string.
    # ------------------------------------------------------------------
    find_program(_UNET_OPENSSL_BIN openssl REQUIRED)

    add_custom_command(
        OUTPUT  ${_UNET_CERT_PEM} ${_UNET_KEY_PEM}
        COMMAND ${_UNET_OPENSSL_BIN} req -x509 -newkey rsa:2048 -sha256 -days 365 -nodes
                -keyout ${_UNET_KEY_PEM}
                -out   ${_UNET_CERT_PEM}
                -subj /CN=localhost
                -addext subjectAltName=DNS:localhost,IP:127.0.0.1
        COMMENT "[unet examples] generating cert.pem + key.pem (OpenSSL)"
        VERBATIM
    )

    add_custom_command(
        OUTPUT  ${_UNET_CERT_PIN}
        COMMAND ${CMAKE_COMMAND} -E env sh -c
                "'${_UNET_OPENSSL_BIN}' x509 -in '${_UNET_CERT_PEM}' -outform DER | '${_UNET_OPENSSL_BIN}' dgst -sha256 -hex | awk '{print $NF}' | tr -d '[:space:]' > '${_UNET_CERT_PIN}'"
        DEPENDS ${_UNET_CERT_PEM}
        COMMENT "[unet examples] computing cert.pin"
        VERBATIM
    )
endif()

if(WIN32)
    add_custom_target(unet_examples_runtime_artifacts DEPENDS
        ${_UNET_CERT_PFX}
        ${_UNET_CERT_PIN}
        ${_UNET_DEMO_FILE}
    )
else()
    add_custom_target(unet_examples_runtime_artifacts DEPENDS
        ${_UNET_CERT_PEM}
        ${_UNET_KEY_PEM}
        ${_UNET_CERT_PIN}
        ${_UNET_DEMO_FILE}
    )
endif()

# ---------------------------------------------------------------------
# Example binaries.
# ---------------------------------------------------------------------
add_executable(HTTPServer HTTPServer.cpp)
target_link_libraries(HTTPServer PRIVATE unet usub::uvent ${_UNET_EXAMPLE_TLS_LIBS})
add_dependencies(HTTPServer unet_examples_runtime_artifacts)

add_executable(WSChat WSChat.cpp)
target_link_libraries(WSChat PRIVATE unet usub::uvent)

add_executable(HTTPClient HTTPClient.cpp)
target_link_libraries(HTTPClient PRIVATE unet usub::uvent ${_UNET_EXAMPLE_TLS_LIBS})

add_executable(VLESS VLESS.cpp)
target_link_libraries(VLESS PRIVATE unet usub::uvent ${_UNET_EXAMPLE_TLS_LIBS})
add_dependencies(VLESS unet_examples_runtime_artifacts)
