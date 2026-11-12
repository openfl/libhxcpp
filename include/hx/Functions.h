#ifndef HX_FUNCTIONS_H
#define HX_FUNCTIONS_H

#include <typeindex>

namespace hx
{
   struct HXCPP_EXTERN_CLASS_ATTRIBUTES LocalFunc : public hx::Object
   {
      int __GetType() const HXCPP_OVERRIDE { return vtFunction; }
      inline void DoMarkThis(hx::MarkContext *__inCtx) { }
#ifdef HXCPP_VISIT_ALLOCS
      inline void DoVisitThis(hx::VisitContext *__inCtx) { }
#endif
   };

   struct HXCPP_EXTERN_CLASS_ATTRIBUTES LocalThisFunc : public LocalFunc
   {
      Dynamic __this;
      void __SetThis(Dynamic inThis) HXCPP_OVERRIDE { __this = inThis; HX_OBJ_WB_GET(this, __this.mPtr); }
      inline void DoMarkThis(hx::MarkContext *__inCtx) { HX_MARK_MEMBER(__this); }
#ifdef HXCPP_VISIT_ALLOCS
      inline void DoVisitThis(hx::VisitContext *__inCtx) { HX_VISIT_MEMBER(__this); }
#endif
   };

#if (HXCPP_API_LEVEL>=500)

    template<typename T1>
    bool IsNotNull(const T1& v1);

    struct HXCPP_EXTERN_CLASS_ATTRIBUTES ErasedCallable_obj : public hx::Object
    {
        virtual std::type_index callableId() const = 0;
    };

    template<class TReturn, class... TArgs>
    class HXCPP_EXTERN_CLASS_ATTRIBUTES Callable_obj;

    template<class TReturn, class... TArgs>
    class HXCPP_EXTERN_CLASS_ATTRIBUTES Callable_obj<TReturn(TArgs...)> : public ErasedCallable_obj
    {
    public:
        HX_IS_INSTANCE_OF enum { _hx_ClassId = ::hx::clsIdClosure };

        int __GetType() const override final
        {
            return vtFunction;
        }

        int __ArgCount() const override final
        {
            return sizeof...(TArgs);
        }

        Dynamic __Run(const Array<Dynamic>& inArgs) override = 0;

        virtual TReturn HX_LOCAL_RUN(TArgs... args) = 0;

        std::type_index callableId() const override
        {
            return std::type_index{ typeid(Callable_obj<TReturn(TArgs...)>) };
        }

        int __Compare(const ::hx::Object* other) const override
        {
            auto otherCallable = dynamic_cast<const ErasedCallable_obj*>(other);
            if (nullptr == otherCallable)
            {
                return -1;
            }

            return callableId() == otherCallable->callableId() ? 0 : -1;
        }
    };

    template<class TReturn, class... TArgs>
    class HXCPP_EXTERN_CLASS_ATTRIBUTES AutoCallable_obj;

#ifdef HXCPP_VISIT_ALLOCS
    // Counts what a callable visits and keeps the object it found
    struct CallableHandleVisitor : public VisitContext
    {
        hx::Object *object = nullptr;
        int count = 0;

        void visitObject(hx::Object **ioPtr) override { object = *ioPtr; count++; }
        void visitAlloc(void **ioPtr) override { count++; }
    };
#endif

    template<class TReturn, class... TArgs>
    class HXCPP_EXTERN_CLASS_ATTRIBUTES AutoCallable_obj<TReturn(TArgs...)> : public ::hx::Callable_obj<TReturn(TArgs...)>
    {
        Dynamic __Run(const Array<Dynamic>& inArgs) override final;

    public:
        // Haxe 5 generates a member closure as a struct holding its receiver in
        //  __this, with no __GetHandle, so weak references lost the closure
        //  while its receiver lived. A callable that visits one object hands
        //  that object back, as the closures of CreateMemberFunctionVar do.
        //  Local functions return 0 through HX_LOCAL_FUNC_NO_HANDLE.
        void* __GetHandle() const override
        {
#ifdef HXCPP_VISIT_ALLOCS
            CallableHandleVisitor visitor;
            const_cast<AutoCallable_obj*>(this)->__Visit(&visitor);
            if (visitor.count == 1)
                return visitor.object;
#endif
            return 0;
        }
    };

    // ::hx::ObjectPtr overloads

    template<class TReturn, class... TArgs>
    class HXCPP_EXTERN_CLASS_ATTRIBUTES Callable;

    template<class TReturn, class... TArgs>
    class Callable<TReturn(TArgs...)> : public ObjectPtr<Callable_obj<TReturn(TArgs...)>>
    {
    public:
        using super = hx::ObjectPtr< Callable_obj<TReturn(TArgs...)> >;
        using OBJ_  = Callable_obj<TReturn(TArgs...)>;
        using Ptr   = Callable_obj<TReturn(TArgs...)>*;

        Callable()
            : super(nullptr) {}

        Callable(const null& inNull)
            : super(nullptr) {}

        Callable(Ptr inPtr) : super(inPtr) {}

        Callable(const hx::ObjectPtr<OBJ_>& inCallable)
            : super(inCallable) {}

        Callable(const Callable<TReturn(TArgs...)>& inCallable)
            : super(inCallable.GetPtr()) {}

        Callable(const ::cpp::Variant& inVariant)
            : Callable(Dynamic(inVariant.asObject())) {}

        // A plain function, such as an extern function used as a value,
        // through the function object that Dynamic makes of it
        template<class TOtherReturn, class... TOtherArgs>
        Callable(TOtherReturn (*inFunction)(TOtherArgs...), typename std::enable_if<IsFunctionValue<TOtherReturn, TOtherArgs...>::value, int>::type = 0)
            : Callable(Dynamic(inFunction)) {}

        Callable(::cpp::Function<TReturn(TArgs...)> inFunction)
            : super(nullptr)
        {
            struct FunctionCallable final : public AutoCallable_obj<TReturn(TArgs...)>
            {
                ::cpp::Function<TReturn(TArgs...)> func;

                FunctionCallable(::cpp::Function<TReturn(TArgs...)> inFunc) : func(inFunc)
                {
                   HX_OBJ_WB_NEW_MARKED_OBJECT(this);
                }

                TReturn HX_LOCAL_RUN(TArgs... args) override
                {
                    return func(args...);
                }
            };

            super::mPtr = new FunctionCallable(inFunction);
        }

        template<class TOtherReturn, class... TOtherArgs>
        Callable(const Callable<TOtherReturn(TOtherArgs...)>& inCallable)
            : super(nullptr)
        {
            struct AdapterCallable final : public AutoCallable_obj<TReturn(TArgs...)>
            {
                Callable<TOtherReturn(TOtherArgs...)> wrapped;

                AdapterCallable(Callable<TOtherReturn(TOtherArgs...)> _wrapped) : wrapped(_wrapped)
                {
                   HX_OBJ_WB_NEW_MARKED_OBJECT(this);
                }

                TReturn HX_LOCAL_RUN(TArgs... args) override
                {
                    return wrapped(args...);
                }

                void* __GetHandle() const override
                {
                    return wrapped.GetPtr();
                }

                std::type_index callableId() const override
                {
                    return wrapped->callableId();
                }

                inline void __Mark(hx::MarkContext* __inCtx) override
                {
                    HX_MARK_MEMBER(wrapped);
                }
#ifdef HXCPP_VISIT_ALLOCS
                inline void __Visit(hx::VisitContext* __inCtx) override
                {
                    HX_VISIT_MEMBER(wrapped);
                }
#endif
            };

            // Converting a null function must stay null - wrapping it made
            // `callback != null` pass and the call then throw
            if (inCallable.GetPtr())
                super::mPtr = new AdapterCallable(inCallable);
        }

        template<class... TOtherArgs>
        Callable(const Callable<void(TOtherArgs...)>& inCallable)
            : super(nullptr)
        {
            struct AdapterCallable final : public AutoCallable_obj<TReturn(TArgs...)>
            {
                Callable<void(TOtherArgs...)> wrapped;

                AdapterCallable(Callable<void(TOtherArgs...)> _wrapped) : wrapped(_wrapped)
                {
                   HX_OBJ_WB_NEW_MARKED_OBJECT(this);
                }

                TReturn HX_LOCAL_RUN(TArgs... args) override
                {
                    wrapped(args...);

                    return null();
                }

                void* __GetHandle() const override
                {
                    return wrapped.GetPtr();
                }

                inline void __Mark(hx::MarkContext* __inCtx) override
                {
                    HX_MARK_MEMBER(wrapped);
                }
#ifdef HXCPP_VISIT_ALLOCS
                inline void __Visit(hx::VisitContext* __inCtx) override
                {
                    HX_VISIT_MEMBER(wrapped);
                }
#endif
            };

            // Converting a null function must stay null - wrapping it made
            // `callback != null` pass and the call then throw
            if (inCallable.GetPtr())
                super::mPtr = new AdapterCallable(inCallable);
        }

        Callable(const Dynamic& inDynamic)
            : super(nullptr)
        {
            auto casted = dynamic_cast<Ptr>(inDynamic.GetPtr());
            if (nullptr != casted)
            {
                super::mPtr = casted;
            }
            else
            {
                if (::hx::IsNotNull(inDynamic) && inDynamic->__GetType() == vtFunction)
                {
                    struct DynamicCallable final : public AutoCallable_obj<TReturn(TArgs...)>
                    {
                        Dynamic wrapped;

                        DynamicCallable(Dynamic _wrapped) : wrapped(_wrapped)
                        {
                           HX_OBJ_WB_NEW_MARKED_OBJECT(this);
                        }

                        TReturn HX_LOCAL_RUN(TArgs... args) override
                        {
                            return wrapped(args...);
                        }

                        void* __GetHandle() const override
                        {
                            return wrapped.GetPtr();
                        }

                        inline void __Mark(hx::MarkContext* __inCtx) override
                        {
                            HX_MARK_MEMBER(wrapped);
                        }
#ifdef HXCPP_VISIT_ALLOCS
                        inline void __Visit(hx::VisitContext* __inCtx) override
                        {
                            HX_VISIT_MEMBER(wrapped);
                        }
#endif
                    };

                    super::mPtr = new DynamicCallable(inDynamic);
                }
            }
        }

        TReturn operator ()(TArgs... args)
        {
            if (nullptr == super::mPtr)
            {
                ::Dynamic::ThrowBadFunctionError();
            }

            return super::mPtr->_hx_run(args...);
        }
    };

    template<class... TArgs>
    class Callable<void(TArgs...)> : public ObjectPtr<Callable_obj<void(TArgs...)>>
    {
    public:
        using super = hx::ObjectPtr< Callable_obj<void(TArgs...)> >;
        using OBJ_ = Callable_obj<void(TArgs...)>;
        using Ptr = Callable_obj<void(TArgs...)>*;

        Callable()
            : super(nullptr) {}

        Callable(const null& inNull)
            : super(nullptr) {}

        Callable(Ptr inPtr) : super(inPtr) {}

        Callable(const hx::ObjectPtr<OBJ_>& inCallable)
            : super(inCallable) {}

        Callable(const Callable<void(TArgs...)>& inCallable)
            : super(inCallable.GetPtr()) {}

        Callable(const ::cpp::Variant& inVariant)
            : Callable(Dynamic(inVariant.asObject())) {}

        // See the other Callable's
        template<class TOtherReturn, class... TOtherArgs>
        Callable(TOtherReturn (*inFunction)(TOtherArgs...), typename std::enable_if<IsFunctionValue<TOtherReturn, TOtherArgs...>::value, int>::type = 0)
            : Callable(Dynamic(inFunction)) {}

        Callable(::cpp::Function<void(TArgs...)> inFunction)
            : super(nullptr)
        {
            struct FunctionCallable final : public AutoCallable_obj<void(TArgs... )>
            {
                ::cpp::Function<void(TArgs...)> func;

                FunctionCallable(::cpp::Function<void(TArgs...)> inFunc) : func(inFunc) {}

                void HX_LOCAL_RUN(TArgs... args) override
                {
                    func(args...);
                }
            };

            super::mPtr = new FunctionCallable(inFunction);
        }

        template<class TOtherReturn, class... TOtherArgs>
        Callable(const Callable<TOtherReturn(TOtherArgs...)>& inCallable)
            : super(nullptr)
        {
            struct AdapterCallable final : public AutoCallable_obj<void(TArgs...)>
            {
                Callable<TOtherReturn(TOtherArgs...)> wrapped;

                AdapterCallable(Callable<TOtherReturn(TOtherArgs...)> _wrapped) : wrapped(_wrapped)
                {
                   HX_OBJ_WB_NEW_MARKED_OBJECT(this);
                }

                void HX_LOCAL_RUN(TArgs... args) override
                {
                    wrapped(args...);
                }

                void* __GetHandle() const override
                {
                    return wrapped.GetPtr();
                }

                std::type_index callableId() const override
                {
                    return wrapped->callableId();
                }

                inline void __Mark(hx::MarkContext* __inCtx) override
                {
                    HX_MARK_MEMBER(wrapped);
                }
#ifdef HXCPP_VISIT_ALLOCS
                inline void __Visit(hx::VisitContext* __inCtx) override
                {
                    HX_VISIT_MEMBER(wrapped);
                }
#endif
            };

            // Converting a null function must stay null - wrapping it made
            // `callback != null` pass and the call then throw
            if (inCallable.GetPtr())
                super::mPtr = new AdapterCallable(inCallable);
        }

        Callable(const Dynamic& inDynamic)
            : super(nullptr)
        {
            auto casted = dynamic_cast<Ptr>(inDynamic.GetPtr());
            if (nullptr != casted)
            {
                super::mPtr = casted;
            }
            else
            {
                if (::hx::IsNotNull(inDynamic) && inDynamic->__GetType() == vtFunction)
                {
                    struct DynamicCallable final : public AutoCallable_obj<void(TArgs...)>
                    {
                        Dynamic wrapped;

                        DynamicCallable(Dynamic _wrapped) : wrapped(_wrapped)
                        {
                           HX_OBJ_WB_NEW_MARKED_OBJECT(this);
                        }

                        void HX_LOCAL_RUN(TArgs... args) override
                        {
                            wrapped(args...);
                        }

                        void* __GetHandle() const override
                        {
                            return wrapped.GetPtr();
                        }

                        inline void __Mark(hx::MarkContext* __inCtx) override
                        {
                            HX_MARK_MEMBER(wrapped);
                        }
#ifdef HXCPP_VISIT_ALLOCS
                        inline void __Visit(hx::VisitContext* __inCtx) override
                        {
                            HX_VISIT_MEMBER(wrapped);
                        }
#endif
                    };

                    super::mPtr = new DynamicCallable(inDynamic);
                }
            }
        }

        void operator ()(TArgs... args)
        {
            if (nullptr == super::mPtr)
            {
                ::Dynamic::ThrowBadFunctionError();
            }

            super::mPtr->_hx_run(args...);
        }
    };
#endif

    // The function object for a plain function used as a value, made by the
    // Dynamic constructor that takes one
#if (HXCPP_API_LEVEL>=500)
    // By value, as the function types Haxe generates take their arguments,
    // so that a typed Callable takes this object as it is
    template<class TReturn, class... TArgs>
    struct StaticFunction_obj final : public AutoCallable_obj<TReturn(typename std::decay<TArgs>::type...)>
    {
        TReturn (*function)(TArgs...);

        StaticFunction_obj(TReturn (*inFunction)(TArgs...)) : function(inFunction)
        {
            HX_OBJ_WB_NEW_MARKED_OBJECT(this);
        }

        TReturn HX_LOCAL_RUN(typename std::decay<TArgs>::type... args) override
        {
            return function(args...);
        }

        void *__GetHandle() const override
        {
            return reinterpret_cast<void *>(function);
        }
    };
#else
    template<std::size_t... I> struct IndexList { };
    template<std::size_t N, std::size_t... I> struct MakeIndexList : MakeIndexList<N - 1, N - 1, I...> { };
    template<std::size_t... I> struct MakeIndexList<0, I...> { typedef IndexList<I...> type; };

    template<class TReturn, class... TArgs>
    struct StaticFunction_obj : public hx::Object
    {
        TReturn (*function)(TArgs...);

        StaticFunction_obj(TReturn (*inFunction)(TArgs...)) : function(inFunction)
        {
            HX_OBJ_WB_NEW_MARKED_OBJECT(this);
        }

        int __GetType() const HXCPP_OVERRIDE { return vtFunction; }
        int __ArgCount() const HXCPP_OVERRIDE { return sizeof...(TArgs); }
        void *__GetHandle() const HXCPP_OVERRIDE { return reinterpret_cast<void *>(function); }

        // Dynamic calls of each arity reach here through hx::Object::__run
        Dynamic __Run(const Array<Dynamic> &inArgs) HXCPP_OVERRIDE
        {
            return call(inArgs, typename MakeIndexList<sizeof...(TArgs)>::type(), std::is_void<TReturn>());
        }

        template<class TArray, std::size_t... I>
        Dynamic call(const TArray &inArgs, IndexList<I...>, std::false_type)
        {
            return function(static_cast<TArgs>(inArgs[I])...);
        }

        template<class TArray, std::size_t... I>
        Dynamic call(const TArray &inArgs, IndexList<I...>, std::true_type)
        {
            function(static_cast<TArgs>(inArgs[I])...);
            return Dynamic();
        }
    };
#endif

    template<typename TReturn, typename... TArgs>
    hx::Object *NewStaticFunction(TReturn (*inFunction)(TArgs...))
    {
        return inFunction ? new StaticFunction_obj<TReturn, TArgs...>(inFunction) : nullptr;
    }
}

#endif
