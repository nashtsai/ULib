// ============================================================================
//
// = LIBRARY
//    ULib - c++ library
//
// = FILENAME
//    macro.h
//
// = AUTHOR
//    Stefano Casazza
//
// ============================================================================

#ifndef ULIB_MACRO_H
#define ULIB_MACRO_H 1

// Design by contract - if (expr == false) then stop

#ifdef DEBUG
#  define U_ASSERT(expr)                 { int _status_ = UTrace::suspend(); U_INTERNAL_ASSERT(expr);                 UTrace::resume(_status_); }
#  define U_ASSERT_MINOR(a,b)            { int _status_ = UTrace::suspend(); U_INTERNAL_ASSERT_MINOR(a,b);            UTrace::resume(_status_); }
#  define U_ASSERT_MAJOR(a,b)            { int _status_ = UTrace::suspend(); U_INTERNAL_ASSERT_MAJOR(a,b);            UTrace::resume(_status_); }
#  define U_ASSERT_EQUALS(a,b)           { int _status_ = UTrace::suspend(); U_INTERNAL_ASSERT_EQUALS(a,b);           UTrace::resume(_status_); }
#  define U_ASSERT_DIFFERS(a,b)          { int _status_ = UTrace::suspend(); U_INTERNAL_ASSERT_DIFFERS(a,b);          UTrace::resume(_status_); }
#  define U_ASSERT_POINTER(ptr)          { int _status_ = UTrace::suspend(); U_INTERNAL_ASSERT_POINTER(ptr);          UTrace::resume(_status_); }
#  define U_ASSERT_RANGE(a,x,b)          { int _status_ = UTrace::suspend(); U_INTERNAL_ASSERT_RANGE(a,x,b);          UTrace::resume(_status_); }

#  define U_ASSERT_MSG(expr,info)        { int _status_ = UTrace::suspend(); U_INTERNAL_ASSERT_MSG(expr,info);        UTrace::resume(_status_); }
#  define U_ASSERT_MINOR_MSG(a,b,info)   { int _status_ = UTrace::suspend(); U_INTERNAL_ASSERT_MINOR_MSG(a,b,info);   UTrace::resume(_status_); }
#  define U_ASSERT_MAJOR_MSG(a,b,info)   { int _status_ = UTrace::suspend(); U_INTERNAL_ASSERT_MAJOR_MSG(a,b,info);   UTrace::resume(_status_); }
#  define U_ASSERT_EQUALS_MSG(a,b,info)  { int _status_ = UTrace::suspend(); U_INTERNAL_ASSERT_EQUALS_MSG(a,b,info);  UTrace::resume(_status_); }
#  define U_ASSERT_DIFFERS_MSG(a,b,info) { int _status_ = UTrace::suspend(); U_INTERNAL_ASSERT_DIFFERS_MSG(a,b,info); UTrace::resume(_status_); }
#  define U_ASSERT_POINTER_MSG(ptr,info) { int _status_ = UTrace::suspend(); U_INTERNAL_ASSERT_POINTER_MSG(ptr,info); UTrace::resume(_status_); }
#  define U_ASSERT_RANGE_MSG(a,x,b,info) { int _status_ = UTrace::suspend(); U_INTERNAL_ASSERT_RANGE_MSG(a,x,b,info); UTrace::resume(_status_); }
#elif defined(U_TEST)
#  define U_ASSERT(expr)                  U_INTERNAL_ASSERT(expr)
#  define U_ASSERT_MINOR(a,b)             U_INTERNAL_ASSERT_MINOR(a,b)
#  define U_ASSERT_MAJOR(a,b)             U_INTERNAL_ASSERT_MAJOR(a,b)
#  define U_ASSERT_EQUALS(a,b)            U_INTERNAL_ASSERT_EQUALS(a,b)
#  define U_ASSERT_DIFFERS(a,b)           U_INTERNAL_ASSERT_DIFFERS(a,b)
#  define U_ASSERT_POINTER(ptr)           U_INTERNAL_ASSERT_POINTER(ptr)
#  define U_ASSERT_RANGE(a,x,b)           U_INTERNAL_ASSERT_RANGE(a,x,b)

#  define U_ASSERT_MSG(expr,info)         U_INTERNAL_ASSERT_MSG(expr,info)
#  define U_ASSERT_MINOR_MSG(a,b,info)    U_INTERNAL_ASSERT_MINOR_MSG(a,b,info)
#  define U_ASSERT_MAJOR_MSG(a,b,info)    U_INTERNAL_ASSERT_MAJOR_MSG(a,b,info)
#  define U_ASSERT_EQUALS_MSG(a,b,info)   U_INTERNAL_ASSERT_EQUALS_MSG(a,b,info)
#  define U_ASSERT_DIFFERS_MSG(a,b,info)  U_INTERNAL_ASSERT_DIFFERS_MSG(a,b,info)
#  define U_ASSERT_POINTER_MSG(ptr,info)  U_INTERNAL_ASSERT_POINTER_MSG(ptr,info)
#  define U_ASSERT_RANGE_MSG(a,x,b,info)  U_INTERNAL_ASSERT_RANGE_MSG(a,x,b,info)
#else
#  define U_ASSERT(expr)
#  define U_ASSERT_MINOR(a,b)
#  define U_ASSERT_MAJOR(a,b)
#  define U_ASSERT_EQUALS(a,b)
#  define U_ASSERT_DIFFERS(a,b)
#  define U_ASSERT_POINTER(ptr)
#  define U_ASSERT_RANGE(a,x,b)

#  define U_ASSERT_MSG(expr,info)
#  define U_ASSERT_MINOR_MSG(a,b,info)
#  define U_ASSERT_MAJOR_MSG(a,b,info)
#  define U_ASSERT_EQUALS_MSG(a,b,info)
#  define U_ASSERT_DIFFERS_MSG(a,b,info)
#  define U_ASSERT_POINTER_MSG(ptr,info)
#  define U_ASSERT_RANGE_MSG(a,x,b,info)
#endif

// Some useful macros for conditionally compiling debug features...

#ifndef HAVE_ARCH64
#  define U_CHECK_MEMORY_SENTINEL      0x0a1b2c3d
#  define U_CHECK_MEMORY_SENTINEL_STR "0x0a1b2c3d" 
#else
#  define U_CHECK_MEMORY_SENTINEL      0x0a1b2c3d4e5f6789
#  define U_CHECK_MEMORY_SENTINEL_STR "0x0a1b2c3d4e5f6789"
#endif

#ifdef DEBUG
// Manage class test for memory corruption

#  define U_MEMORY_TEST         UMemoryError memory;
#  define U_MEMORY_TEST_COPY(o)   memory = o.memory;

#  define U_CHECK_MEMORY_OBJECT(p) U_ASSERT_MACRO((p)->memory.invariant(), "ERROR ON MEMORY", (p)->memory.getErrorType(p))
#  define U_CHECK_MEMORY           U_CHECK_MEMORY_OBJECT(this)

// Manage info on execution of program

#  define U_TRACE(level,args...) UTrace utr(level , ##args);

// NB: U_DUMP, U_SYSCALL() and U_RETURN() depend on presence of U_TRACE()

#  define U_INTERNAL_DUMP(args...) { if (utr.active[0])                                     u_trace_dump(args); }
#  define          U_DUMP(args...) { if (utr.active[0]) { int _status_ = UTrace::suspend(); u_trace_dump(args); UTrace::resume(_status_); } }

#  define U_SYSCALL(name,format,args...)        (utr.trace_syscall("::"#name"(" format ")" , ##args), \
                                                 utr.trace_sysreturn_type(::name(args)))

#  define U_SYSCALL_NO_PARAM(name)              (utr.trace_syscall("::"#name"()",0), \
                                                 utr.trace_sysreturn_type(::name()))

#  define U_SYSCALL_VOID(name,format,args...)   {utr.trace_syscall("::"#name"(" format ")" , ##args); \
                                                 name(args); utr.trace_sysreturn(false,0);}

#  define U_SYSCALL_VOID_NO_PARAM(name)         {utr.trace_syscall("::"#name"()",0); \
                                                 name(); utr.trace_sysreturn(false,0);}

#  define U_RETURN(r)                                                 return (utr.trace_return_type((r)))
#  define U_RETURN_STRING(str) {U_INTERNAL_ASSERT((str).invariant()); return (utr.trace_return("%.*S",U_STRING_TO_TRACE((str))),(str));}
#  define U_RETURN_OBJECT(obj)                                        return (utr.trace_return(  "%O",U_OBJECT_TO_TRACE((obj))),(obj))
#  define U_RETURN_POINTER(ptr,type)                                  return ((type*)utr.trace_return_type((void*)(ptr)))

#  define U_MEMCPY(a,b,n) (void) U_SYSCALL(u__memcpy, "%p,%p,%u,%S",(void*)(a),(const void*)(b),(n),__PRETTY_FUNCTION__)

// Dump argument for exec()...

#  define U_DUMP_EXEC(argv, envp) { uint32_t _i; \
for (_i = 0; argv[_i]; ++_i) \
   { \
   U_INTERNAL_DUMP("argv[%2u] = %p %S", _i, argv[_i], argv[_i]) \
   } \
U_INTERNAL_DUMP("argv[%2u] = %p %S", _i, argv[_i], argv[_i]) \
if (envp) \
   { \
   for (_i = 0; envp[_i]; ++_i) \
      { \
      U_INTERNAL_DUMP("envp[%2u] = %p %S", _i, envp[_i], envp[_i]) \
      } \
   U_INTERNAL_DUMP("envp[%2u] = %p %S", _i, envp[_i], envp[_i]) \
   } }

// Dump attributes...

#  define U_DUMP_ATTRS(attrs) { uint32_t _i; for (_i = 0; attrs[_i]; ++_i) { U_INTERNAL_DUMP("attrs[%2u] = %S", _i, attrs[_i]) } }

#ifdef _MSWINDOWS_
#  define U_FORK()                     -1
#  define U_VFORK()                    -1
#else
#  define U_FORK()                     u_debug_fork(  ::fork(), utr.active[0])
#  define U_VFORK()                    u_debug_vfork(::vfork(), utr.active[0])
#endif

#  define U_EXIT(exit_value)           { if (utr.active[0]) u_debug_exit(exit_value); ::exit(exit_value); }
#  define U_EXEC(pathname, argv, envp) u_debug_exec(pathname, argv, envp, utr.active[0])

// Dump fd_set...

#ifndef __FDS_BITS
#  ifdef _MSWINDOWS_
#     define __FDS_BITS(fd_set) ((fd_set)->fd_array)
#  else
#     define __FDS_BITS(fd_set) ((fd_set)->fds_bits)
#  endif
#endif

#else /* DEBUG */

#  define U_MEMORY_TEST
#  define U_MEMORY_TEST_COPY(o)
#  define U_CHECK_MEMORY
#  define U_CHECK_MEMORY_OBJECT(p)

#  define U_TRACE(level,args...)
#  define U_DUMP(args...)
#  define U_INTERNAL_DUMP(args...)
#  define U_SYSCALL(name,format,args...)        ::name(args)
#  define U_SYSCALL_VOID(name,format,args...)   ::name(args)
#  define U_SYSCALL_NO_PARAM(name)              ::name()
#  define U_SYSCALL_VOID_NO_PARAM(name)         ::name()

#  define U_MEMCPY(a,b,n) (void) memcpy((void*)(a),(const void*)(b),(n))

#  define U_RETURN(r)                  return (r)
#  define U_RETURN_STRING(r)           return (r)
#  define U_RETURN_OBJECT(obj)         return (obj)
#  define U_RETURN_POINTER(ptr,type)   return ((type*)ptr)

#  define U_OBJECT_TO_TRACE(object)

#  define U_DUMP_ATTRS(attrs)
#  define U_DUMP_EXEC(argv, envp)

#ifdef _MSWINDOWS_
#  define U_FORK()                     -1
#  define U_VFORK()                    -1
#else
#  define U_FORK()                      ::fork()
#  define U_VFORK()                    ::vfork()
#endif

#  define U_EXIT(exit_value)           ::exit(exit_value)
#  define U_EXEC(pathname, argv, envp) u_exec_failed = false; ::execve(pathname, argv, envp); u_exec_failed = true; \
                                       U_WARNING("::execve(%s,%p,%p) = -1%R", pathname, argv, envp, NULL); \
                                       ::_exit(EX_UNAVAILABLE)
#endif /* DEBUG */

// A mechanism that allow all objects to be registered with a
// central in-memory "database" that can dump the state of all live objects

#if defined(DEBUG) && defined(U_STDCPP_ENABLE)

#  define U_REGISTER_OBJECT_PTR(level,CLASS,ptr) \
            if (UObjectDB::fd > 0 && \
                (level) >= UObjectDB::level_active) { \
                  UObjectDB::registerObject(new UObjectDumpable_Adapter<CLASS>(level, #CLASS, ptr, &(ptr->memory._this))); }

#  define U_REGISTER_OBJECT_PTR_WITHOUT_CHECK_MEMORY(level,CLASS,ptr) \
            if (UObjectDB::fd > 0 && \
                (level) >= UObjectDB::level_active) { \
                  UObjectDB::registerObject(new UObjectDumpable_Adapter<CLASS>(level, #CLASS, ptr, 0)); }

#  define U_REGISTER_OBJECT(                     level,CLASS) U_REGISTER_OBJECT_PTR(                     level,CLASS,this)
#  define U_REGISTER_OBJECT_WITHOUT_CHECK_MEMORY(level,CLASS) U_REGISTER_OBJECT_PTR_WITHOUT_CHECK_MEMORY(level,CLASS,this)

#  define U_UNREGISTER_OBJECT(level,ptr) \
            if (UObjectDB::fd > 0 && \
                (level) >= UObjectDB::level_active) { \
                UObjectDB::unregisterObject(ptr); }

#  define U_TRACE_REGISTER_OBJECT(level,CLASS,format,args...) if (UObjectDB::flag_new_object == false) U_SET_LOCATION_INFO; \
                                                                  UObjectDB::flag_new_object =  false; \
                                                              U_REGISTER_OBJECT(level,CLASS) \
                                                              UTrace utr(level, #CLASS"::"#CLASS"(" format ")" , ##args);

#  define U_TRACE_REGISTER_OBJECT_WITHOUT_CHECK_MEMORY(level,CLASS,format,args...) \
                                                              if (UObjectDB::flag_new_object == false) U_SET_LOCATION_INFO; \
                                                                  UObjectDB::flag_new_object =  false; \
                                                              U_REGISTER_OBJECT_WITHOUT_CHECK_MEMORY(level,CLASS) \
                                                              UTrace utr(level, #CLASS"::"#CLASS"(" format ")" , ##args);

#  define U_TRACE_UNREGISTER_OBJECT(level,CLASS)              U_UNREGISTER_OBJECT(level,this) \
                                                              UTrace utr(level, #CLASS"::~"#CLASS"()");

#  define U_DUMP_OBJECT(msg,obj) \
            if (UObjectDB::fd > 0) { \
               char _buffer[4096]; \
               uint32_t _n = UObjectDB::dumpObject(_buffer, sizeof(_buffer), obj); \
               if (utr.active[0]) u_trace_dump(msg " = \n%.*s\n", U_min(_n,4000), _buffer); }

#  define U_WRITE_MEM_POOL_INFO_TO(fmt,args...)  UMemoryPool::writeInfoTo(fmt,args)

// Manage location info for object allocation

#  define U_NEW(args...)                         (U_SET_LOCATION_INFO, UObjectDB::flag_new_object = true, new args)
#  define U_NEW_DBG(CLASS,obj,args...)           (UMemoryPool::obj_class = #CLASS, \
                                                  UMemoryPool::func_call = __PRETTY_FUNCTION__, \
                                                  obj = U_NEW(args), \
                                                  UMemoryPool::obj_class = UMemoryPool::func_call = 0)

#  define U_NEW_ULIB_OBJECT(obj,args...)         UObjectDB::flag_ulib_object = true, \
                                                 obj = U_NEW(args), \
                                                 UObjectDB::flag_ulib_object = false

#else // DEBUG && U_STDCPP_ENABLE

#  define U_REGISTER_OBJECT(                           level,CLASS)
#  define U_REGISTER_OBJECT_WITHOUT_CHECK_MEMORY(      level,CLASS)
#  define U_REGISTER_OBJECT_PTR(                       level,CLASS,ptr)
#  define U_REGISTER_OBJECT_PTR_WITHOUT_CHECK_MEMORY(  level,CLASS,ptr)
#  define U_TRACE_REGISTER_OBJECT(                     level,CLASS,format,args...)
#  define U_TRACE_REGISTER_OBJECT_WITHOUT_CHECK_MEMORY(level,CLASS,format,args...)
#  define U_UNREGISTER_OBJECT(level,pointer)
#  define U_TRACE_UNREGISTER_OBJECT(level,CLASS)

#  define U_DUMP_OBJECT(msg,obj)
#  define U_WRITE_MEM_POOL_INFO_TO(fmt,args...)

#  define U_NEW(args...)                               new args
#  define U_NEW_DBG(CLASS,obj,args...)           obj = new args
#  define U_NEW_ULIB_OBJECT(obj,args...)         obj = new args

#endif // DEBUG && U_STDCPP_ENABLE

#endif
