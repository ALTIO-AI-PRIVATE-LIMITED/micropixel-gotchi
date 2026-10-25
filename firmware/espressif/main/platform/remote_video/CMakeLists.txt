list(APPEND micropixel_sources
    "platform/remote_video/https_remote_video.cpp"
)
list(APPEND micropixel_requires esp_https_server esp_http_server)

# iOS gives a page the camera only over HTTPS, so the Gotchi serves the page
# with a self-signed certificate. It is made once per checkout and kept out of
# git, so a rebuild keeps the certificate the phone already accepted.
set(micropixel_remote_video_cert_dir "${CMAKE_CURRENT_LIST_DIR}/../../../../../.private/remote-video")
set(micropixel_remote_video_cert "${micropixel_remote_video_cert_dir}/remote_video_cert.pem")
set(micropixel_remote_video_key "${micropixel_remote_video_cert_dir}/remote_video_key.pem")
if(NOT CMAKE_BUILD_EARLY_EXPANSION AND NOT EXISTS "${micropixel_remote_video_key}")
    find_program(micropixel_openssl openssl REQUIRED)
    file(MAKE_DIRECTORY "${micropixel_remote_video_cert_dir}")
    execute_process(
        COMMAND "${micropixel_openssl}" req -x509 -newkey rsa:2048 -nodes -sha256 -days 825
                -subj "/CN=CGotchi" -keyout "${micropixel_remote_video_key}" -out "${micropixel_remote_video_cert}"
        RESULT_VARIABLE micropixel_openssl_result
        OUTPUT_QUIET ERROR_QUIET)
    if(NOT micropixel_openssl_result EQUAL 0)
        message(FATAL_ERROR "openssl could not make the Remote Video certificate")
    endif()
endif()
