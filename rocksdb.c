
#ifdef HAVE_CONFIG_H
#include <rdf_config.h>
#endif

#ifdef WIN32
#include <win32_rdf_config.h>
#endif

#include <stdio.h>
#include <string.h>
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <sys/types.h>

#include <redland.h>
#include <rdf_storage.h>
#include <rdf_heuristics.h>

#include "impl.h"

typedef struct
{

    librdf_storage *storage;

    int is_new;
  
    char *name;
    size_t name_len;

    implementation* impl;

} librdf_storage_rocksdb_instance;

typedef enum { SPO, POS, OSP } index_type;

/* prototypes for local functions */
static int librdf_storage_rocksdb_init(
    librdf_storage* storage, const char *name, librdf_hash* options
);
static int librdf_storage_rocksdb_open(
    librdf_storage* storage, librdf_model* model
);
static int librdf_storage_rocksdb_close(librdf_storage* storage);
static int librdf_storage_rocksdb_size(librdf_storage* storage);
static int librdf_storage_rocksdb_add_statement(librdf_storage* storage, librdf_statement* statement);
static int librdf_storage_rocksdb_add_statements(librdf_storage* storage, librdf_stream* statement_stream);
static int librdf_storage_rocksdb_remove_statement(librdf_storage* storage, librdf_statement* statement);
static int librdf_storage_rocksdb_contains_statement(librdf_storage* storage, librdf_statement* statement);
static librdf_stream* librdf_storage_rocksdb_serialise(librdf_storage* storage);
static librdf_stream* librdf_storage_rocksdb_find_statements(librdf_storage* storage, librdf_statement* statement);

/* serialising implementing functions */
static int rocksdb_results_stream_end_of_stream(void* context);
static int rocksdb_results_stream_next_statement(void* context);
static void* rocksdb_results_stream_get_statement(void* context, int flags);
static void rocksdb_results_stream_finished(void* context);

/* context functions */
static int librdf_storage_rocksdb_context_add_statement(librdf_storage* storage, librdf_node* context_node, librdf_statement* statement);
static int librdf_storage_rocksdb_context_remove_statement(librdf_storage* storage, librdf_node* context_node, librdf_statement* statement);
static int librdf_storage_rocksdb_context_contains_statement(librdf_storage* storage, librdf_node* context, librdf_statement* statement);
static librdf_stream* librdf_storage_rocksdb_context_serialise(librdf_storage* storage, librdf_node* context_node);

/* helper functions for contexts */

static librdf_iterator* librdf_storage_rocksdb_get_contexts(librdf_storage* storage);

/* transactions */
static int librdf_storage_rocksdb_transaction_start(librdf_storage *storage);
static int librdf_storage_rocksdb_transaction_commit(librdf_storage *storage);
static int librdf_storage_rocksdb_transaction_rollback(librdf_storage *storage);

static void librdf_storage_rocksdb_register_factory(librdf_storage_factory *factory);
#ifdef MODULAR_LIBRDF
void librdf_storage_module_register_factory(librdf_world *world);
#endif


/* functions implementing storage api */
static int
librdf_storage_rocksdb_init(librdf_storage* storage, const char *name,
                           librdf_hash* options)
{

    char *name_copy;
    librdf_storage_rocksdb_instance* context;
  
    if(!name) {
	if(options)
	    librdf_free_hash(options);
	return 1;
    }
  
    context = LIBRDF_CALLOC(librdf_storage_rocksdb_instance*, 1,
			    sizeof(*context));
    if(!context) {
	if(options)
	    librdf_free_hash(options);
	return 1;
    }

    librdf_storage_set_instance(storage, context);
  
    context->storage = storage;
    context->name_len = strlen(name);
//    context->transaction = 0;

    name_copy = LIBRDF_MALLOC(char*, context->name_len + 1);
    if(!name_copy) {
	if(options)
	    librdf_free_hash(options);
	return 1;
    }

    strcpy(name_copy, name);
    context->name = name_copy;

    if (librdf_hash_get_as_boolean(options, "new") > 0)
	context->is_new = 1;
    else
	context->is_new = 0;

    // Add options here.
    int sync = librdf_hash_get_as_boolean(options, "sync");
    if (sync < 0) { sync = 0; }

    /* no more options, might as well free them now */
    if(options)
	librdf_free_hash(options);

    context->impl = implementation_new(context->name, sync, context->is_new);

    return 0;

}


static void
librdf_storage_rocksdb_terminate(librdf_storage* storage)
{

    librdf_storage_rocksdb_instance* context;

    context = (librdf_storage_rocksdb_instance*)storage->instance;

    context->impl->free(context->impl);
  
    if (context == NULL)
	return;

    if(context->name)
	LIBRDF_FREE(char*, context->name);
  
    LIBRDF_FREE(librdf_storage_rocksdb_terminate, storage->instance);

}

static
char* node_helper(librdf_storage* storage, librdf_node* node)
{

    librdf_uri* uri;
    librdf_uri* dt_uri;

    const char* integer_type = "http://www.w3.org/2001/XMLSchema#integer";
    const char* float_type = "http://www.w3.org/2001/XMLSchema#float";
    const char* datetime_type = "http://www.w3.org/2001/XMLSchema#dateTime";

    char* name;
    char data_type;

    switch(librdf_node_get_type(node)) {

    case LIBRDF_NODE_TYPE_RESOURCE:
	uri = librdf_node_get_uri(node);
	name = librdf_uri_as_string(uri);
	data_type = 'u';
	break;
	
    case LIBRDF_NODE_TYPE_LITERAL:
	dt_uri = librdf_node_get_literal_value_datatype_uri(node);
	if (dt_uri == 0)
	    data_type = 's';
	else {
	    const char* type_uri = librdf_uri_as_string(dt_uri);
	    if (strcmp(type_uri, integer_type) == 0)
		data_type = 'i';
	    else if (strcmp(type_uri, float_type) == 0)
		data_type = 'f';
	    else if (strcmp(type_uri, datetime_type) == 0)
		data_type = 'd';
	    else
		data_type = 's';
	}
	name = librdf_node_get_literal_value(node);
	break;

    case LIBRDF_NODE_TYPE_BLANK:
	name = librdf_node_get_blank_identifier(node);
	data_type = 'b';
	break;

    case LIBRDF_NODE_TYPE_UNKNOWN:
	break;
	
    }

    char* term = malloc(5 + strlen(name));
    if (term == 0) {
	fprintf(stderr, "malloc failed");
	return 0;
    }
    
    sprintf(term, "%c:%s", data_type, name);

    return term;

}

static int
statement_helper(librdf_storage* storage,
		 librdf_statement* statement,
		 librdf_node* context,
		 char** s, char** p, char** o, char** c)
{

    librdf_node* sn = librdf_statement_get_subject(statement);
    librdf_node* pn = librdf_statement_get_predicate(statement);
    librdf_node* on = librdf_statement_get_object(statement);

    if (sn)
	*s = node_helper(storage, sn);
    else
	*s = 0;
    
    if (pn)
	*p = node_helper(storage, pn);
    else
	*p = 0;

    if (on)
	*o = node_helper(storage, on);
    else
	*o = 0;

    if (context)
	*c = node_helper(storage, context);
    else
	*c = 0;

}

static int
librdf_storage_rocksdb_open(librdf_storage* storage, librdf_model* model)
{

    librdf_storage_rocksdb_instance* context;

    context = (librdf_storage_rocksdb_instance*)storage->instance;

    return context->impl->open(context->impl);

}


/**
 * librdf_storage_rocksdb_close:
 * @storage: the storage
 *
 * Close the rocksdb storage.
 * 
 * Return value: non 0 on failure
 **/
static int
librdf_storage_rocksdb_close(librdf_storage* storage)
{

    librdf_storage_rocksdb_instance* context;
    context = (librdf_storage_rocksdb_instance*)storage->instance;

    context->impl->close(context->impl);

}

static int
librdf_storage_rocksdb_size(librdf_storage* storage)
{
    
    librdf_storage_rocksdb_instance* context;
    context = (librdf_storage_rocksdb_instance*)storage->instance;

    return context->impl->size(context->impl);
	
}

static int
librdf_storage_rocksdb_add_statement(librdf_storage* storage, 
                                    librdf_statement* statement)
{

    return librdf_storage_rocksdb_context_add_statement(storage, NULL,
							statement);
}

static int
librdf_storage_rocksdb_add_statements(librdf_storage* storage,
                                     librdf_stream* statement_stream)
{


    librdf_storage_rocksdb_instance* context;
    context = (librdf_storage_rocksdb_instance*)storage->instance;

    for(; !librdf_stream_end(statement_stream);
	librdf_stream_next(statement_stream)) {

	librdf_statement* statement;
	librdf_node* context_node;
    
	statement = librdf_stream_get_object(statement_stream);
	context_node = librdf_stream_get_context2(statement_stream);

	if(!statement) {
	    break;
	}

	char* s;
	char* p;
	char* o;
	char* c;
	statement_helper(storage, statement, context_node, &s, &p, &o, &c);

	int ret = context->impl->add(context->impl, s, p, o, c);

	free(s);
	free(p);
	free(o);
	free(c);

	if (ret < 0) return -1;

    }
    
    return 0;

}


static int
librdf_storage_rocksdb_remove_statement(librdf_storage* storage,
                                       librdf_statement* statement)
{
    return librdf_storage_rocksdb_context_remove_statement(storage, NULL, 
							  statement);
}

static int
librdf_storage_rocksdb_contains_statement(librdf_storage* storage, 
                                         librdf_statement* statement)
{
    return librdf_storage_rocksdb_context_contains_statement(storage, NULL,
							    statement);
}


static int
librdf_storage_rocksdb_context_contains_statement(librdf_storage* storage,
                                                 librdf_node* context_node,
                                                 librdf_statement* statement)
{

    librdf_storage_rocksdb_instance* context;
    context = (librdf_storage_rocksdb_instance*)storage->instance;

    char* s;
    char* p;
    char* o;
    char* c;
    statement_helper(storage, statement, context_node, &s, &p, &o, &c);

    int ret = context->impl->contains(context->impl, s, p, o, c);

    free(s);
    free(p);
    free(o);
    free(c);

    return ret;

}

typedef struct {
    
    librdf_storage *storage;
    librdf_storage_rocksdb_instance* rocksdb_context;

    // FIXME: Needed?
    librdf_statement *statement;
    librdf_node* context;

    implementation_stream* stream;

} rocksdb_results_stream;

static
librdf_node* node_constructor_helper(librdf_world* world, const char* t,
				     size_t len)
{

    librdf_node* o;

    if ((strlen(t) < 2) || (t[1] != ':')) {
	fprintf(stderr, "node_constructor_helper called on invalid term\n");
	return 0;
    }

    if (t[0] == 'u') {
 	o = librdf_new_node_from_counted_uri_string(world,
						    (unsigned char*) t + 2,
						    len - 2);
	return o;
    }

    if (t[0] == 's') {
	o = librdf_new_node_from_typed_counted_literal(world,
						       (unsigned char*) t + 2,
						       len - 2, 0, 0, 0);
	return o;
    }


    if (t[0] == 'i') {
	librdf_uri* dt =
	    librdf_new_uri(world,
			   "http://www.w3.org/2001/XMLSchema#integer");
	if (dt == 0)
	    return 0;

	o = librdf_new_node_from_typed_counted_literal(world, t + 2, len - 2,
						       0, 0, dt);
	librdf_free_uri(dt);
	return o;
    }
    
    if (t[0] == 'f') {
	librdf_uri* dt =
	    librdf_new_uri(world,
			   "http://www.w3.org/2001/XMLSchema#float");
	if (dt == 0)
	    return 0;

	o = librdf_new_node_from_typed_counted_literal(world, t + 2, len - 2,
						       0, 0, dt);
	librdf_free_uri(dt);
	return o;
    }

    if (t[0] == 'd') {
	librdf_uri* dt =
	    librdf_new_uri(world,
			   "http://www.w3.org/2001/XMLSchema#dateTime");
	if (dt == 0)
	    return 0;

	o = librdf_new_node_from_typed_counted_literal(world, t + 2, len - 2,
						       0, 0, dt);
	librdf_free_uri(dt);
	return o;
    }    

    return librdf_new_node_from_typed_counted_literal(world,
						      (unsigned char*) t + 2,
						      len - 2, 0, 0, 0);

}

static int
rocksdb_results_stream_end_of_stream(void* context)
{

    rocksdb_results_stream* scontext;
    scontext = (rocksdb_results_stream*)context;

    return scontext->stream->at_end(scontext->stream);

}


static int
rocksdb_results_stream_next_statement(void* context)
{

    rocksdb_results_stream* scontext;
    scontext = (rocksdb_results_stream*)context;

    return scontext->stream->next(scontext->stream);

}


static void*
rocksdb_results_stream_get_statement(void* context, int flags)
{

    rocksdb_results_stream* scontext;

    const char* s;
    size_t s_len;
    const char* p;
    size_t p_len;
    const char* o;
    size_t o_len;
	
    scontext = (rocksdb_results_stream*)context;

    switch(flags) {

    case LIBRDF_ITERATOR_GET_METHOD_GET_OBJECT:

	scontext->stream->get_s(scontext->stream, &s, &s_len);
	scontext->stream->get_p(scontext->stream, &p, &p_len);
	scontext->stream->get_o(scontext->stream, &o, &o_len);

	if (scontext->statement) {
	    librdf_free_statement(scontext->statement);
	    scontext->statement = 0;
	}

	librdf_node* sn, * pn, * on;
	sn = node_constructor_helper(scontext->storage->world, s, s_len);
	pn = node_constructor_helper(scontext->storage->world, p, p_len);
	on = node_constructor_helper(scontext->storage->world, o, o_len);

	if (sn == 0 || pn == 0 || on == 0) {
	    if (sn) librdf_free_node(sn);
	    if (pn) librdf_free_node(pn);
	    if (on) librdf_free_node(on);
	    return 0;
	}

	scontext->statement =
	    librdf_new_statement_from_nodes(scontext->storage->world,
					    sn, pn, on);

	return scontext->statement;

    case LIBRDF_ITERATOR_GET_METHOD_GET_CONTEXT:
	return scontext->context;

    default:
	librdf_log(scontext->storage->world,
		   0, LIBRDF_LOG_ERROR, LIBRDF_FROM_STORAGE, NULL,
		   "Unknown iterator method flag %d", flags);
	return NULL;
    }
    
}

static void
rocksdb_results_stream_finished(void* context)
{

    rocksdb_results_stream* scontext;
    scontext = (rocksdb_results_stream*)context;

    if (scontext->stream)
	scontext->stream->free(scontext->stream);
	
    if(scontext->storage)
	librdf_storage_remove_reference(scontext->storage);

    // FIXME: Are we using statement?
    if(scontext->statement)
	librdf_free_statement(scontext->statement);

    if(scontext->context)
	librdf_free_node(scontext->context);

    LIBRDF_FREE(librdf_storage_rocksdb_find_statements_stream_context, scontext);

}

static librdf_stream*
librdf_storage_rocksdb_serialise(librdf_storage* storage)
{

    librdf_statement* stmt = 
	    librdf_new_statement_from_nodes(storage->world,
					    0, 0, 0);

    librdf_stream* strm = librdf_storage_rocksdb_find_statements(storage,
								   stmt);

    librdf_free_statement(stmt);

    return strm;

}


/**
 * librdf_storage_rocksdb_find_statements:
 * @storage: the storage
 * @statement: the statement to match
 *
 * .
 * 
 * Return a stream of statements matching the given statement (or
 * all statements if NULL).  Parts (subject, predicate, object) of the
 * statement can be empty in which case any statement part will match that.
 * Uses #librdf_statement_match to do the matching.
 * 
 * Return value: a #librdf_stream or NULL on failure
 **/
static librdf_stream*
librdf_storage_rocksdb_find_statements(librdf_storage* storage,
				       librdf_statement* statement)
{
  
    librdf_storage_rocksdb_instance* context;
    rocksdb_results_stream* scontext;
    librdf_stream* stream;
    char* s;
    char* p;
    char* o;
    char* c;

    context = (librdf_storage_rocksdb_instance*)storage->instance;
    
    statement_helper(storage, statement, 0, &s, &p, &o, &c);

    implementation_stream* strm = context->impl->new_stream(
	context->impl,
	s, p, o, c);

    if (strm == NULL) {
	free(s);
	free(p);
	free(o);
	free(c);
	return NULL;
    }

    scontext =
	LIBRDF_CALLOC(rocksdb_results_stream*, 1, sizeof(*scontext));
    if(!scontext)
	return NULL;

    scontext->storage = storage;
    librdf_storage_add_reference(scontext->storage);

    scontext->rocksdb_context = context;
    scontext->stream = strm;

#ifdef DEBUG
    fprintf(stderr, "Query: ");
    if (s)
      fprintf(stderr, "s=%s ", s);
    if (p)
      fprintf(stderr, "p=%s ", p);
    if (o)
      fprintf(stderr, "o=%s ", o);
    fprintf(stderr, "\n");
#endif
 
    stream =
	librdf_new_stream(storage->world,
			  (void*)scontext,
			  &rocksdb_results_stream_end_of_stream,
			  &rocksdb_results_stream_next_statement,
			  &rocksdb_results_stream_get_statement,
			  &rocksdb_results_stream_finished);
    if(!stream) {
	rocksdb_results_stream_finished((void*)scontext);
	return NULL;
    }
  
    return stream;
    
}

/**
 * librdf_storage_rocksdb_context_add_statement:
 * @storage: #librdf_storage object
 * @context_node: #librdf_node object
 * @statement: #librdf_statement statement to add
 *
 * Add a statement to a storage context.
 * 
 * Return value: non 0 on failure
 **/
static int
librdf_storage_rocksdb_context_add_statement(librdf_storage* storage,
                                            librdf_node* context_node,
                                            librdf_statement* statement) 
{

    char* s;
    char* p;
    char* o;
    char* c;

    statement_helper(storage, statement, context_node, &s, &p, &o, &c);

    librdf_storage_rocksdb_instance* context; 
    context = (librdf_storage_rocksdb_instance*)storage->instance;

    int ret = context->impl->add(context->impl, s, p, o, c);

    free(s);
    free(p);
    free(o);
    free(c);

    return ret;

}


/**
 * librdf_storage_rocksdb_context_remove_statement:
 * @storage: #librdf_storage object
 * @context_node: #librdf_node object
 * @statement: #librdf_statement statement to remove
 *
 * Remove a statement from a storage context.
 * 
 * Return value: non 0 on failure
 **/
static int
librdf_storage_rocksdb_context_remove_statement(librdf_storage* storage, 
                                               librdf_node* context_node,
                                               librdf_statement* statement) 
{

    librdf_storage_rocksdb_instance* context; 
    context = (librdf_storage_rocksdb_instance*)storage->instance;

    char* s;
    char* p;
    char* o;
    char* c;

    statement_helper(storage, statement, context_node, &s, &p, &o, &c);

    int ret = context->impl->remove(context->impl, s, p, o, c);

    free(s);
    free(p);
    free(o);
    free(c);

    return 0;

}


static  int
librdf_storage_rocksdb_context_remove_statements(librdf_storage* storage, 
                                                librdf_node* context_node)
{

    //FIXME: Not implemented.

    return -1;

}

/**
 * librdf_storage_rocksdb_context_serialise:
 * @storage: #librdf_storage object
 * @context_node: #librdf_node object
 *
 * Rocksdb all statements in a storage context.
 * 
 * Return value: #librdf_stream of statements or NULL on failure or context is empty
 **/
static librdf_stream*
librdf_storage_rocksdb_context_serialise(librdf_storage* storage,
                                        librdf_node* context_node) 
{

    //FIXME: Not implemented.

    return 0;

}

/**
 * librdf_storage_rocksdb_context_get_contexts:
 * @storage: #librdf_storage object
 *
 * Rocksdb all context nodes in a storage.
 * 
 * Return value: #librdf_iterator of context_nodes or NULL on failure or no contexts
 **/
static librdf_iterator*
librdf_storage_rocksdb_get_contexts(librdf_storage* storage) 
{
    // FIXME: Not implemented.

    return 0;

}

/**
 * librdf_storage_rocksdb_get_feature:
 * @storage: #librdf_storage object
 * @feature: #librdf_uri feature property
 *
 * Get the value of a storage feature.
 * 
 * Return value: #librdf_node feature value or NULL if no such feature
 * exists or the value is empty.
 **/
static librdf_node*
librdf_storage_rocksdb_get_feature(librdf_storage* storage, librdf_uri* feature)
{
    /* librdf_storage_rocksdb_instance* scontext; */
    unsigned char *uri_string;

    /* scontext = (librdf_storage_rocksdb_instance*)storage->instance; */

    if(!feature)
	return NULL;

    uri_string = librdf_uri_as_string(feature);
    if(!uri_string)
	return NULL;

    // FIXME: This is a lie.  Contexts not implemented. :-/
    if(!strcmp((const char*)uri_string, LIBRDF_MODEL_FEATURE_CONTEXTS)) {
	return librdf_new_node_from_typed_literal(storage->world,
						  (const unsigned char*)"1",
						  NULL, NULL);
    }

    return NULL;
}


/**
 * librdf_storage_rocksdb_transaction_start:
 * @storage: #librdf_storage object
 *
 * Start a new transaction unless one is already active.
 * 
 * Return value: 0 if transaction successfully started, non-0 on error
 * (including a transaction already active)
 **/
static int
librdf_storage_rocksdb_transaction_start(librdf_storage *storage)
{
#ifdef BROKEN

    librdf_storage_rocksdb_instance* context;

    context = (librdf_storage_rocksdb_instance*)storage->instance;

    /* If already have a trasaction, silently do nothing. */
    if (context->transaction)
	return 0;

    context->transaction = rocksdb_elements_create();
    if (context->transaction == 0)
	return -1;

    return 0;
#endif

}


/**
 * librdf_storage_rocksdb_transaction_commit:
 * @storage: #librdf_storage object
 *
 * Commit an active transaction.
 * 
 * Return value: 0 if transaction successfully committed, non-0 on error
 * (including no transaction active)
 **/
static int
librdf_storage_rocksdb_transaction_commit(librdf_storage *storage)
{

#ifdef BROKEN
    librdf_storage_rocksdb_instance* context;

    context = (librdf_storage_rocksdb_instance*)storage->instance;

    if (context->transaction == 0)
	return -1;

    int ret = rocksdb_add_elements(context->comms, context->transaction);

    rocksdb_elements_free(context->transaction);

    context->transaction = 0;

    if (ret < 0) return -1;

    return 0;

#endif
}


/**
 * librdf_storage_rocksdb_transaction_rollback:
 * @storage: #librdf_storage object
 *
 * Roll back an active transaction.
 * 
 * Return value: 0 if transaction successfully committed, non-0 on error
 * (including no transaction active)
 **/
static int
librdf_storage_rocksdb_transaction_rollback(librdf_storage *storage)
{
#ifdef BROKEN

    librdf_storage_rocksdb_instance* context;

    context = (librdf_storage_rocksdb_instance*)storage->instance;

    if (context->transaction)
	return -1;

    rocksdb_elements_free(context->transaction);

    context->transaction = 0;

    return 0;
#endif

}

/** Local entry point for dynamically loaded storage module */
static void
librdf_storage_rocksdb_register_factory(librdf_storage_factory *factory) 
{
    LIBRDF_ASSERT_CONDITION(!strcmp(factory->name, "rocksdb"));

    factory->version            = LIBRDF_STORAGE_INTERFACE_VERSION;
    factory->init               = librdf_storage_rocksdb_init;
    factory->terminate          = librdf_storage_rocksdb_terminate;
    factory->open               = librdf_storage_rocksdb_open;
    factory->close              = librdf_storage_rocksdb_close;
    factory->size               = librdf_storage_rocksdb_size;
    factory->add_statement      = librdf_storage_rocksdb_add_statement;
    factory->add_statements     = librdf_storage_rocksdb_add_statements;
    factory->remove_statement   = librdf_storage_rocksdb_remove_statement;
    factory->contains_statement = librdf_storage_rocksdb_contains_statement;
    factory->serialise          = librdf_storage_rocksdb_serialise;
    factory->find_statements    = librdf_storage_rocksdb_find_statements;
    factory->context_add_statement    = librdf_storage_rocksdb_context_add_statement;
    factory->context_remove_statement = librdf_storage_rocksdb_context_remove_statement;
    factory->context_remove_statements = librdf_storage_rocksdb_context_remove_statements;
    factory->context_serialise        = librdf_storage_rocksdb_context_serialise;
    factory->get_contexts             = librdf_storage_rocksdb_get_contexts;
    factory->get_feature              = librdf_storage_rocksdb_get_feature;
    factory->transaction_start        = librdf_storage_rocksdb_transaction_start;
    factory->transaction_commit       = librdf_storage_rocksdb_transaction_commit;
    factory->transaction_rollback     = librdf_storage_rocksdb_transaction_rollback;
}

#ifdef MODULAR_LIBRDF

/** Entry point for dynamically loaded storage module */
void
librdf_storage_module_register_factory(librdf_world *world)
{
    librdf_storage_register_factory(world, "rocksdb", "Rocksdb",
				    &librdf_storage_rocksdb_register_factory);
}

#else

/*
 * librdf_init_storage_rocksdb:
 * @world: world object
 *
 * INTERNAL - Initialise the built-in storage_rocksdb module.
 */
void
librdf_init_storage_rocksdb(librdf_world *world)
{
    librdf_storage_register_factory(world, "rocksdb", "rocksdb",
				    &librdf_storage_rocksdb_register_factory);
}

#endif

