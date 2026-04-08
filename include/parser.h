#ifndef PARSER_H
#define PARSER_H

#include "ast.h"
#include "lexer.h"

/**
 * INSERT INTO <ident> VALUES ( <value_list> ) [ ; ]
 * 성공 시 *out 에 InsertStmt(힙). 호출자가 ast_insert_stmt_free.
 * @return 0 성공, -1 실패(구문/렉스 오류; *out == NULL).
 */
int parser_parse_insert(Lexer *lex, InsertStmt **out);

#endif
