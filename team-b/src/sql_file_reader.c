#include "sql_file_reader.h"

#include <stdio.h>
#include <stdlib.h>

int read_text_file(const char *path, char **out_contents)
{
    FILE *file;
    long file_size;
    size_t bytes_read;
    char *buffer;

    /* path는 읽을 파일 경로, out_contents는 결과 문자열을 돌려줄 출력 포인터다. */
    if (path == NULL || out_contents == NULL) {
        return 1;
    }

    /* 실패하더라도 호출자 쪽 포인터가 안전하게 NULL 상태를 유지하도록 먼저 초기화한다. */
    *out_contents = NULL;

    /* "rb"는 read binary의 약자다. 텍스트 파일이지만 줄바꿈 변환 없이 그대로 읽기 위해 사용한다. */
    file = fopen(path, "rb");
    if (file == NULL) {
        return 1;
    }

    /*
     * fseek(file, 0L, SEEK_END):
     * 파일 위치 커서를 "끝"으로 이동한다. 성공적으로 이동하면 return 0
     * 아직 읽지는 않고, 파일 크기를 알아내기 위한 준비 단계다.
     */
    if (fseek(file, 0L, SEEK_END) != 0) {
        /* fclose는 열어 둔 파일을 닫는 함수다. 실패 경로에서도 반드시 호출해야 한다. */
        fclose(file);
        return 1;
    }

    /*
     * ftell(file):
     * 현재 파일 위치를 숫자로 돌려준다.
     * 방금 파일 끝으로 이동했으므로, 이 값이 곧 파일 크기다.
     */
    file_size = ftell(file);
    if (file_size < 0) {
        fclose(file);
        return 1;
    }

    /*
     * 실제 내용을 읽으려면 다시 파일 처음으로 돌아와야 한다.
     * SEEK_SET은 "파일 시작점 기준"을 뜻한다.
     */
    if (fseek(file, 0L, SEEK_SET) != 0) {
        fclose(file);
        return 1;
    }

    /*
     * 파일 전체 내용을 담을 버퍼를 만든다. malloc으로 그 버퍼의 메모리를 잡아준다. (버퍼 자체는 스택, 버퍼가 가리키는 대상은 힙)
     * +1은 마지막에 문자열 종료 문자 '\0'을 넣기 위한 칸이다.
     */
    buffer = (char *)malloc((size_t)file_size + 1U);
    if (buffer == NULL) {
        fclose(file);
        return 1;
    }

    /*
     * fread(buffer, 1, file_size, file):
     * 파일에서 file_size 바이트를 읽어서 buffer에 채운다.
     * 여기서는 "파일 전체를 한 번에 읽기"를 하는 부분이다.
     * fread(어디에저장할지, 한덩어리크기, 몇덩어리읽을지, 어느파일에서읽을지)
     * file에서 1바이트짜리 데이터를 file_size개 읽어서 buffer에 복사해라.
     */
    bytes_read = fread(buffer, 1U, (size_t)file_size, file);
    if (bytes_read != (size_t)file_size) {
        free(buffer);
        fclose(file);
        return 1;
    }

    /* C 문자열로 다루기 위해 마지막 칸에 '\0'을 직접 넣는다. */
    // 파일은 그냥 바이트들의 덩어리, fread는 자동으로 '\0'을 안 붙인다.
    // fread는 문자열 함수가 아니라, 그냥 파일의 raw byte를 메모리에 복사하는 함수라서 \0을 따로 꼭 붙여줘야 C 문자열처럼 처리할 수 있다.
    buffer[(size_t)file_size] = '\0';

    /* 파일에서 읽는 일은 끝났으므로 파일 핸들을 닫는다. */
    fclose(file);

    /* 이제 호출자는 out_contents를 통해 읽은 전체 문자열을 받게 된다. */
    *out_contents = buffer;
    return 0;
}
