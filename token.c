/*
  +----------------------------------------------------------------------+
  | PHP Version 5                                                        |
  +----------------------------------------------------------------------+
  | Copyright (c) 1997-2006 The PHP Group                                |
  +----------------------------------------------------------------------+
  | This source file is subject to version 3.0 of the PHP license,       |
  | that is bundled with this package in the file LICENSE, and is        |
  | available through the world-wide-web at the following url:           |
  | http://www.php.net/license/3_0.txt.                                  |
  | If you did not receive a copy of the PHP license and are unable to   |
  | obtain it through the world-wide-web, please send a note to          |
  | license@php.net so we can mail you a copy immediately.               |
  +----------------------------------------------------------------------+
  | Author: Sara Golemon <pollita@php.net>                               |
  | Heavily borrowed from php-src:ext/tokenizer/tokenizer.c              |
  |   by Andrei Zmievski <andrei@php.net>                                |
  +----------------------------------------------------------------------+
*/

/* $Id$ */

#include "php_token.h"

#include "zend_language_scanner_defs.h"
#include "zend_language_scanner.h"
#include "zend_language_parser.h"
#include "zend_stream.h"

ZEND_DECLARE_MODULE_GLOBALS(token);
zend_class_entry *php_token_ce;

#define TOKEN_PARSE       1

typedef int (*tokenizer_t)(zend_long flags,
                           on_token_callback_t on_token,
                           on_feedback_callback_t on_feedback,
                           void *data);

/* {{{ array_append_object */
static int array_append_object(void *data, int id,
                               const char *text, int text_len,
                               int lineno) {
	zval *tokens = (zval*)data;
	zval object;
	object_init_ex(&object, php_token_ce);
	zend_update_property_long(php_token_ce, &object,
	                          "id", sizeof("id") - 1, id);
	zend_update_property_stringl(php_token_ce, &object,
	                          "text", sizeof("text") - 1, (char*)text, text_len);
	zend_update_property_long(php_token_ce, &object,
	                          "lineno", sizeof("lineno") - 1, lineno);
	add_next_index_zval(tokens, &object);

	return SUCCESS;
}
/* }}} */

/* {{{ array_update_object */
static int array_update_object(void *data, int id) {
	zend_array *tokens = Z_ARRVAL_P((zval*)data);
	int num_tokens = zend_hash_num_elements(tokens);
	zval *token;
	token = zend_hash_index_find(tokens, num_tokens - 1);
	if (token) {
		zend_update_property_long(php_token_ce, token,
		  "id", sizeof("id") - 1, id);
	}
	return SUCCESS;
}
/* }}} */

/* {{{ tokenize */
static int tokenize_scan(zend_long flags, on_token_callback_t on_token,
                         on_feedback_callback_t on_feedback, void *data) {
	int token_id, token_line = 0, need_tokens = -1;
	zval token;

	LANG_SCNG(yy_state) = yycINITIAL;
	ZVAL_UNDEF(&token);
	while ((token_id = lex_scan(&token))) {
		zval object;

		if (token_id == T_CLOSE_TAG && LANG_SCNG(yy_text)[LANG_SCNG(yy_leng) - 1] != '>') {
			CG(zend_lineno)++;
		}

		on_token(data, token_id, LANG_SCNG(yy_text), LANG_SCNG(yy_leng), token_line);

		if (Z_TYPE(token) != IS_UNDEF) {
			zval_dtor(&token);
			ZVAL_UNDEF(&token);
		}

		if (need_tokens != -1) {
			if (token_id != T_WHITESPACE &&
			    token_id != T_OPEN_TAG &&
			    token_id != T_COMMENT &&
			    token_id != T_DOC_COMMENT &&
			    --need_tokens == 0) {
				if (LANG_SCNG(yy_cursor) != LANG_SCNG(yy_limit)) {
					on_token(data, token_id, LANG_SCNG(yy_text), LANG_SCNG(yy_leng), token_line);
				}
				break;
			}
		} else if (token_id == T_HALT_COMPILER) {
			need_tokens = 3;
		}

		token_line = CG(zend_lineno);
	}
	return SUCCESS;
}
/* }}} */

/* {{{ tokenize_parse */
static void tokenize_parse_event(zend_php_scanner_event event, int token, int line) {
	switch (event) {
		case ON_TOKEN:
			if (token == END) break;
			TOKENG(on_token)(TOKENG(callback_data), token,
			                 (char *)LANG_SCNG(yy_text), LANG_SCNG(yy_leng),
			                 line);
			break;
		case ON_FEEDBACK:
			TOKENG(on_feedback)(TOKENG(callback_data), token);
			break;
		case ON_STOP:
			TOKENG(on_token)(TOKENG(callback_data), T_INLINE_HTML,
			                 (char *)LANG_SCNG(yy_cursor),
			                 LANG_SCNG(yy_limit) - LANG_SCNG(yy_cursor),
			                 CG(zend_lineno));
			break;
	}
}

static int tokenize_parse(zend_long flags, on_token_callback_t on_token,
                          on_feedback_callback_t on_feedback, void *data) {
	zend_ast *original_ast = CG(ast);
	zend_arena *original_ast_arena = CG(ast_arena);
	zend_bool original_in_compilation = CG(in_compilation);
	zend_bool ret;

	CG(ast) = NULL;
	CG(ast_arena) = zend_arena_create(1024 * 32);

	LANG_SCNG(yy_state) = yycINITIAL;
	LANG_SCNG(on_event) = tokenize_parse_event;
	TOKENG(on_token) = on_token;
	TOKENG(on_feedback) = on_feedback;
	TOKENG(callback_data) = data;

	ret = zendparse();
	zend_ast_destroy(CG(ast));
	zend_arena_destroy(CG(ast_arena));

	CG(in_compilation) = original_in_compilation;
	CG(ast_arena) = original_ast_arena;
	CG(ast) = original_ast;

	return ret;
}
/* }}} */

/* {{{ do_tokenize */
static void do_tokenize(INTERNAL_FUNCTION_PARAMETERS, zend_bool is_file) {
	tokenizer_t tokenizer;
	zend_lex_state original_lex_state;
	zend_file_handle handle;
	zend_string *source;
	zend_long flags = 0;
	zend_bool ret;
	zval zsource;

	if (FAILURE ==
	    zend_parse_parameters(ZEND_NUM_ARGS(), "S|l", &source, &flags)) {
		return;
	}

	tokenizer = (flags & TOKEN_PARSE) ? tokenize_parse : tokenize_scan;
	zend_save_lexical_state(&original_lex_state);
	if (is_file) {
		handle.filename = ZSTR_VAL(source);
		handle.free_filename = 0;
		handle.type = ZEND_HANDLE_FILENAME;
		handle.opened_path = NULL;
		handle.handle.fp = NULL;
		ret = open_file_for_scanning(&handle);
	} else {
		ZVAL_STR(&zsource, source);
		ret = zend_prepare_string_for_scanning(&zsource, "");
	}

	if (ret == SUCCESS) {
		array_init(return_value);
		if (FAILURE == tokenizer(flags, array_append_object,
		                         array_update_object, return_value)) {
			zval_dtor(return_value);
			ZVAL_NULL(return_value);
		}
	}
	if (is_file) {
		zend_destroy_file_handle(&handle);
	} else {
		zval_dtor(&zsource);
	}
	zend_restore_lexical_state(&original_lex_state);

}

/* {{{ proto array Token::tokenizeString(string $script[ int $flags = 0])
Essentially token_get_all(), but returns an array of Token objects
*/
ZEND_BEGIN_ARG_INFO_EX(Token_tokenizeString_arginfo, 0, ZEND_RETURN_VALUE, 0)
	ZEND_ARG_INFO(0, script)
	ZEND_ARG_INFO(0, flags)
ZEND_END_ARG_INFO();
static PHP_METHOD(Token, tokenizeString) {
	do_tokenize(INTERNAL_FUNCTION_PARAM_PASSTHRU, 0);
} /* }}} */

/* {{{ proto array Token::tokenizeFile(string $filename[, int $flags = 0])
Parse directly from a file, rather than a string
*/
ZEND_BEGIN_ARG_INFO_EX(Token_tokenizeFile_arginfo, 0, ZEND_RETURN_VALUE, 0)
	ZEND_ARG_INFO(0, filename)
	ZEND_ARG_INFO(0, flags)
ZEND_END_ARG_INFO();
static PHP_METHOD(Token, tokenizeFile) {
	do_tokenize(INTERNAL_FUNCTION_PARAM_PASSTHRU, 1);
} /* }}} */

/* {{{ proto string Token::name(int $id) */
ZEND_BEGIN_ARG_INFO_EX(Token_name_arginfo, 0, ZEND_RETURN_VALUE, 0)
	ZEND_ARG_INFO(0, id)
ZEND_END_ARG_INFO();
static void (*token_name)(INTERNAL_FUNCTION_PARAMETERS) = NULL;
static PHP_METHOD(Token, name) {
	/* This is a hack to avoid having to re-generate the token map */
	if (!token_name) {
		zend_string *s_token_name =
		  zend_string_init("token_name", sizeof("token_name") - 1, 0);
		zend_function *func =
		  zend_hash_find_ptr(EG(function_table), s_token_name);
		zend_string_release(s_token_name);

		if (!func) {
			php_error_docref(NULL, E_RECOVERABLE_ERROR,
			  "Call to undefined function token_name()");
			return;
		}
		if (!func->type == ZEND_INTERNAL_FUNCTION) {
			php_error_docref(NULL, E_RECOVERABLE_ERROR,
			  "token_name() is an unexpected type");
			return;
		}
		token_name = func->internal_function.handler;
	}

	token_name(INTERNAL_FUNCTION_PARAM_PASSTHRU);
} /* }}} */

/* {{{ methods */
static zend_function_entry token_methods[] = {
	PHP_ME(Token, tokenizeString, Token_tokenizeString_arginfo, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
	PHP_ME(Token, tokenizeFile,   Token_tokenizeFile_arginfo,   ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
	PHP_ME(Token, name,           Token_name_arginfo,           ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
	PHP_FE_END
};
/* }}} */

/* {{{ MINIT */
static PHP_MINIT_FUNCTION(token) {
	zend_class_entry ce;

	INIT_CLASS_ENTRY(ce, "Token", token_methods);
	php_token_ce = zend_register_internal_class(&ce);

	zend_declare_class_constant_long(php_token_ce,
	  "PARSE", sizeof("PARSE") - 1, TOKEN_PARSE);

	zend_declare_property_long(php_token_ce,
	  "id", sizeof("id") - 1, 0, ZEND_ACC_PUBLIC);
	zend_declare_property_string(php_token_ce,
	  "text", sizeof("text") - 1, "", ZEND_ACC_PUBLIC);
	zend_declare_property_long(php_token_ce,
	  "lineno", sizeof("lineno") - 1, 0, ZEND_ACC_PUBLIC);

	return SUCCESS;
} /* }}} */

/* {{{ token_module_entry */
zend_module_entry token_module_entry = {
	STANDARD_MODULE_HEADER,
	"token",
	NULL, /* functions */
	PHP_MINIT(token),
	NULL, /* MSHUTDOWN */
	NULL, /* RINIT */
	NULL, /* RSHUTDOWN */
	NULL, /* MIFNO */
	"0.0.1",
	PHP_MODULE_GLOBALS(token),
	NULL, /* GINIT */
	NULL, /* GSHUTDOWN */
	NULL, /* RPOSTSHUTDOWN */
	STANDARD_MODULE_PROPERTIES_EX
}; /* }}} */

#ifdef COMPILE_DL_TOKEN
#ifdef ZTS
ZEND_TSRMLS_CACHE_DEFINE();
#endif
ZEND_GET_MODULE(token)
#endif

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */
