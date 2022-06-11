#include "tree_sitter.h"

extern VALUE mTreeSitter;

VALUE cLogger;

// This type is layed out in the DATA_* style.
// data: the TSLogger object
// payload: what will be used in TSLogger.log()
//          therefore: data.payload = payload
typedef struct {
  TSLogger data;
  VALUE payload;
  VALUE format;
} logger_t;

static const char *logger_log_type_str(TSLogType log_type) {
  switch (log_type) {
  case TSLogTypeParse:
    return "Parse:";
  case TSLogTypeLex:
    return "Lex  :";
  default:
    return "?????:";
  }
}

static void logger_log_printf(void *ptr, TSLogType log_type,
                              const char *message) {
  logger_t *logger = (logger_t *)ptr;
  VALUE type = rb_str_new_cstr(logger_log_type_str(log_type));
  VALUE msg = rb_str_new_cstr(message);

  rb_funcall(logger->payload, rb_intern("printf"), 3, logger->format, type,
             msg);
}

static void logger_log_puts(void *ptr, TSLogType log_type,
                            const char *message) {
  logger_t *logger = (logger_t *)ptr;
  VALUE str = rb_sprintf("%s: %s", logger_log_type_str(log_type), message);

  rb_funcall(logger->payload, rb_intern("puts"), 1, str);
}

static void logger_log_write(void *ptr, TSLogType log_type,
                             const char *message) {

  logger_t *logger = (logger_t *)ptr;
  VALUE str = rb_sprintf("%s: %s\n", logger_log_type_str(log_type), message);

  rb_funcall(logger->payload, rb_intern("write"), 1, str);
}

static void logger_payload_set(logger_t *logger, VALUE value) {
  logger->payload = value;
  logger->data.payload = (void *)logger;

  if (!NIL_P(logger->format) && !NIL_P(logger->payload)) {
    if (rb_respond_to(logger->payload, rb_intern("printf"))) {
      logger->data.log = logger_log_printf;
    } else if (rb_respond_to(logger->payload, rb_intern("puts"))) {
      logger->data.log = &logger_log_puts;
    } else {
      logger->data.log = &logger_log_write;
    }
  } else if (!NIL_P(logger->payload)) {
    logger->data.log = &logger_log_write;
  }
}

static void logger_free(void *ptr) { xfree(ptr); }

static size_t logger_memsize(const void *ptr) {
  logger_t *type = (logger_t *)ptr;
  return sizeof(type);
}

static void logger_mark(void *ptr) {
  logger_t *logger = (logger_t *)ptr;
  rb_gc_mark_movable(logger->payload);
  rb_gc_mark_movable(logger->format);
}

static void logger_compact(void *ptr) {
  logger_t *logger = (logger_t *)ptr;
  logger->payload = rb_gc_location(logger->payload);
  logger->format = rb_gc_location(logger->format);
}

const rb_data_type_t logger_data_type = {
    .wrap_struct_name = "logger",
    .function =
        {
            .dmark = logger_mark,
            .dfree = logger_free,
            .dsize = logger_memsize,
            .dcompact = logger_compact,
        },
    .flags = RUBY_TYPED_FREE_IMMEDIATELY,
};

static VALUE logger_allocate(VALUE klass) {
  logger_t *logger;
  return TypedData_Make_Struct(klass, logger_t, &logger_data_type, logger);
}

VALUE new_logger(const TSLogger *ptr) {
  if (ptr != NULL) {
    VALUE res = logger_allocate(cLogger);
    logger_t *logger;
    TypedData_Get_Struct(res, logger_t, &logger_data_type, logger);

    logger->data = *ptr;
    logger->data.payload = logger;

    VALUE payload = Qnil;
    VALUE format = Qnil;
    if (ptr->payload != NULL) {
      logger_t *old_logger = (logger_t *)ptr->payload;
      payload = old_logger->payload;
      format = old_logger->format;
    }
    logger_payload_set(logger, payload);
    logger->format = format;

    return res;
  } else {
    return Qnil;
  }
}

TSLogger *value_to_logger(VALUE self) {
  logger_t *obj;
  TypedData_Get_Struct(self, logger_t, &logger_data_type, obj);
  return &obj->data;
}

static void logger_initialize_stderr(logger_t *logger) {
  VALUE stderr = rb_gv_get("$stderr");
  if (!NIL_P(stderr)) {
    logger_payload_set(logger, stderr);
  } else {
    logger_payload_set(logger, Qnil);
  }
}

// For now, we only take:
// argv[0] = stream
// argv[1] = format : String
//
// We need to add support for argv[1] : lambda/block
//
// case argv[1]
// in lambda => lambda
// in String => puts || printf || write
// else      => write
// end
//
static VALUE logger_initialize(int argc, VALUE *argv, VALUE self) {
  logger_t *logger;
  TypedData_Get_Struct(self, logger_t, &logger_data_type, logger);

  VALUE payload;
  VALUE format;
  rb_scan_args(argc, argv, "02", &payload, &format);

  logger->format = format;

  if (argv == 0) {
    logger_initialize_stderr(logger);
  } else {
    logger_payload_set(logger, payload);
  }

  return self;
}

static VALUE logger_inspect(VALUE self) {
  logger_t *logger;

  TypedData_Get_Struct(self, logger_t, &logger_data_type, logger);
  return rb_sprintf("{payload=%+" PRIsVALUE ", format=%+" PRIsVALUE "}",
                    logger->payload, logger->format);
}

DATA_FAST_FORWARD_FNV(logger, write, payload)
DATA_FAST_FORWARD_FNV(logger, puts, payload)
DATA_FAST_FORWARD_FNV(logger, printf, payload)

ACCESSOR(logger, format)
ACCESSOR(logger, payload)

void init_logger(void) {
  cLogger = rb_define_class_under(mTreeSitter, "Logger", rb_cObject);

  rb_define_alloc_func(cLogger, logger_allocate);

  /* Class methods */
  rb_define_method(cLogger, "initialize", logger_initialize, -1);
  DEFINE_ACCESSOR(cLogger, logger, format)
  DEFINE_ACCESSOR(cLogger, logger, payload)
  rb_define_method(cLogger, "write", logger_write, -1);
  rb_define_method(cLogger, "puts", logger_puts, -1);
  rb_define_method(cLogger, "printf", logger_printf, -1);
  rb_define_method(cLogger, "inspect", logger_inspect, 0);
}
