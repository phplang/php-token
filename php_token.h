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
  +----------------------------------------------------------------------+
*/

/* $Id$ */

#ifndef PHP_TOKEN_H
#define PHP_TOKEN_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"

typedef int (*on_token_callback_t)(void*, int id,
                                   const char *text, int len,
                                   int lineno);

typedef int (*on_feedback_callback_t)(void*, int id);

ZEND_BEGIN_MODULE_GLOBALS(token)
	on_token_callback_t on_token;
	on_feedback_callback_t on_feedback;
	void *callback_data;
ZEND_END_MODULE_GLOBALS(token);

ZEND_EXTERN_MODULE_GLOBALS(token);
#define TOKENG(v) ZEND_MODULE_GLOBALS_ACCESSOR(token, v)
extern zend_module_entry token_module_entry;
#define phpext_token_ptr &token_module_entry

#endif  /* PHP_TOKEN_H */

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */
