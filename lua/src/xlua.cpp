#include "xlua.hpp"
#include "xlua_prototypes.hpp"
#include <hpx/lcos/broadcast.hpp>

const int max_output_args = 10;

#define CHECK_STRING(INDEX,NAME) \
  if(!lua_isstring(L,INDEX)) { \
    luai_writestringerror("Argument to '%s' is not a string ",NAME);\
    return 0; \
  }

#ifndef luai_writestringerror
#define luai_writestringerror(s,p) \
        (fprintf(stderr, (s), (p)), fflush(stderr))
#endif

namespace hpx {
std::string env = "_ENV";

void show(std::ostream& o,ptr_type p);

void show(std::ostream& o,Holder h) {
  int w = h.var.which();
  if(w == Holder::empty_t) {
    o << "*nil*";
  } else if(w == Holder::num_t) {
    o << boost::get<double>(h.var);
  } else if(w == Holder::str_t) {
    o << boost::get<std::string>(h.var);
  } else if(w == Holder::table_t) {
    table_ptr tp = boost::get<table_ptr>(h.var);
    o << "{";
    for(auto i = tp->t.begin();i != tp->t.end();++i) {
      if(i != tp->t.begin())
        o << ",";
      key_type kt = i->first;
      if(kt.which()==0)
        o << boost::get<double>(kt);
      else
        o << boost::get<std::string>(kt);
      o << "=";
      show(o,i->second);
    }
    o << "}";
  } else {
    o << "type=" << w;
  }
}

void show(std::ostream& o,ptr_type p) {
  o << "{";
  for(int i=0;i<p->size();i++) {
    if(i > 0) o << ",";
    Holder& h = (*p)[i];
    show(o,h);
  }
  o << "}";
}

const char *table_metatable_name = "table";
const char *vector_metatable_name = "vector_num";
const char *table_iter_metatable_name = "table_iter";
const char *future_metatable_name = "hpx_future";
const char *guard_metatable_name = "hpx_guard";
const char *locality_metatable_name = "hpx_locality";
const char *lua_client_metatable_name = "lua_client";

const char *hpx_metatable_name = "hpx";

table_ptr globals{new table_inner};

const char *lua_read(lua_State *L,void *data,size_t *size);
int lua_write(lua_State *L,const char *str,unsigned long len,std::string *buf);
bool cmp_meta(lua_State *L,int index,const char *meta_name);

  Lua::Lua() : busy(true), L(luaL_newstate()) {
    luaL_openlibs(L);
    lua_pushcfunction(L,xlua_stop);
    lua_setglobal(L,"stop");
    lua_pushcfunction(L,xlua_start);
    lua_setglobal(L,"start");
    lua_pushcfunction(L,xlua_get_value);
    lua_setglobal(L,"get_value");
    lua_pushcfunction(L,xlua_get_counter);
    lua_setglobal(L,"get_counter");
    lua_pushcfunction(L,discover);
    lua_setglobal(L,"discover_counter_types");
    lua_pushcfunction(L,make_ready_future);
    lua_setglobal(L,"make_ready_future");
    lua_pushcfunction(L,dataflow);
    lua_setglobal(L,"dataflow");
    lua_pushcfunction(L,xlua_unwrapped);
    lua_setglobal(L,"unwrapped");
    lua_pushcfunction(L,call);
    lua_setglobal(L,"call");
    lua_pushcfunction(L,async);
    lua_setglobal(L,"async");
    lua_pushcfunction(L,vector_pop);
    lua_setglobal(L,"vector_pop");
    lua_pushcfunction(L,luax_wait_all);
    lua_setglobal(L,"wait_all");
    lua_pushcfunction(L,luax_when_all);
    lua_setglobal(L,"when_all");
    lua_pushcfunction(L,luax_when_any);
    lua_setglobal(L,"when_any");
    lua_pushcfunction(L,unwrap);
    lua_setglobal(L,"unwrap");
    lua_pushcfunction(L,isfuture);
    lua_setglobal(L,"isfuture");
    lua_pushcfunction(L,isvector);
    lua_setglobal(L,"isvector");
    lua_pushcfunction(L,islocality);
    lua_setglobal(L,"islocality");
    lua_pushcfunction(L,istable);
    lua_setglobal(L,"istable");
    lua_pushcfunction(L,hpx_reg);
    lua_setglobal(L,"HPX_PLAIN_ACTION");
    lua_pushcfunction(L,hpx_run);
    lua_setglobal(L,"hpx_run");
    lua_pushcfunction(L,luax_run_guarded);
    lua_setglobal(L,"run_guarded");
    lua_pushcfunction(L,find_here);
    lua_setglobal(L,"find_here");
    lua_pushcfunction(L,all_localities);
    lua_setglobal(L,"find_all_localities");
    lua_pushcfunction(L,remote_localities);
    lua_setglobal(L,"find_remote_localities");
    lua_pushcfunction(L,root_locality);
    lua_setglobal(L,"find_root_locality");
//    lua_pushcfunction(L,apex_register_policy);
//    lua_setglobal(L,"apex_register_policy");

    //open_hpx(L);
    luaL_requiref(L, "hpx",&open_hpx, 1);
    open_table(L);
    luaL_requiref(L, "table_t", &open_table, 1);
    luaL_requiref(L, "vector_t", &open_vector, 1);
    open_table_iter(L);
    luaL_requiref(L, "table_iter_t", &open_table_iter, 1);
    open_future(L);
    luaL_requiref(L, "future", &open_future, 1);
    open_guard(L);
    luaL_requiref(L, "guard",&open_guard, 1);
    open_locality(L);
    luaL_requiref(L, "locality",&open_locality, 1);
    luaL_requiref(L, "component",&open_component, 1);
    lua_pop(L,lua_gettop(L));

    new_table(L);
    table_ptr *tp = (table_ptr *)lua_touserdata(L,-1);
    *tp = globals;
    lua_setglobal(L,"globals");
    luaL_dostring(L,
      " function for_each_s(i0,ihi,f)"
      "  local i"
      "  for i=i0,ihi do"
      "    f(i)"
      "  end"
      " end"
      ""
      " function for_each(lo,hi,f,gr)"
      "  local i0,ihi,fs"
      "  if gr == nil then"
      "    gr = math.floor((hi-lo+1)/8)"
      "  end"
      "  if gr <= 0 then"
      "    gr = 1"
      "  end"
      "  fs = {}"
      "  for i0=lo,hi,gr do"
      "    ihi = math.min(hi,i0+gr-1)"
      "    fs[#fs+1]=async(for_each_s,i0,ihi,f)"
      "  end"
      "  wait_all(fs)"
      " end"
  );

    for(auto i=function_registry.begin();i != function_registry.end();++i) {
      // Insert into table
      if(lua_load(L,(lua_Reader)lua_read,(void *)&i->second,i->first.c_str(),"b") != 0) {
        std::cout << "function " << i->first << " size=" << i->second.size() << std::endl;
        SHOW_ERROR(L);
      } else {
        lua_setglobal(L,i->first.c_str());
      }
    }
    /*
    luaL_dostring(L,
"function __hpx_nextvalue(obj)"
"  local t = setmetatable({object=obj},{"
"    __call = function(self,v,k)"
"      local nk = self.obj.find(k)"
"      if nk != nil then"
"        return nk,self.obj[nk]"
"      end"
"    end"
"  })"
"  return t"
"end");
*/
    busy = false;
  }
  void Holder::unpack(lua_State *L) {
    if(var.which() == num_t) {
      lua_pushnumber(L,boost::get<double>(var));
    } else if(var.which() == str_t) {
      lua_pushstring(L,boost::get<std::string>(var).c_str());
    } else if(var.which() == ptr_t) {
      auto ptr = boost::get<ptr_type >(var);
      for(auto i=ptr->begin();i != ptr->end();++i)
        i->unpack(L);
    } else if(var.which() == fut_t) {
      // Shouldn't ever happen
      //std::cout << "ERROR: Unrealized future in arg list" << std::endl;
      //abort();
      new_future(L);
      future_type *fc = (future_type *)lua_touserdata(L,-1);
      *fc = boost::get<future_type>(var);
    } else if(var.which() == vector_t) {
      new_vector(L);
      vector_ptr *tp = (vector_ptr *)lua_touserdata(L,-1);
      *tp = boost::get<vector_ptr>(var);
    } else if(var.which() == locality_t) {
      new_locality(L);
      hpx::naming::id_type *tp = (hpx::naming::id_type *)lua_touserdata(L,-1);
      *tp = boost::get<hpx::naming::id_type>(var);
    } else if(var.which() == client_t) {
      new_component(L);
      lua_aux_client *tp = (lua_aux_client*)lua_touserdata(L,-1);
      *tp = boost::get<lua_aux_client>(var);
    } else if(var.which() == table_t) {
      new_table(L);
      table_ptr *tp = (table_ptr *)lua_touserdata(L,-1);
      *tp = boost::get<table_ptr>(var);
      /*
      try {
        table_ptr& table = boost::get<table_ptr>(var);
        lua_createtable(L,0,table->size());
        for(auto i=table->begin();i != table->end();++i) {
          const int which = i->first.which();
          if(which == 0) { // number
            double d = boost::get<double>(i->first);
            lua_pushnumber(L,d);
            if(d == 0) {
              std::cout << "unpack:PRINT=" << (*this) << std::endl;
              abort();
            }
          } else if(which == 1) { // string
            std::string str = boost::get<std::string>(i->first);
            lua_pushstring(L,str.c_str());
          } else {
            std::cout << "ERROR: Unknown key type: " << which << std::endl;
            abort();
          }
          i->second.unpack(L);
          lua_settable(L,-3);
        }
      } catch(std::exception e) {
        std::cout << "EX2=" << e.what() << std::endl;
      }
      */
    } else if(var.which() == bytecode_t) {
      Bytecode& bc = boost::get<Bytecode>(var);
      lua_load(L,(lua_Reader)lua_read,(void *)&bc.data,0,"b");
    } else if(var.which() == closure_t) {
      closure_ptr cp = boost::get<closure_ptr>(var);
      lua_load(L,(lua_Reader)lua_read,(void *)&cp->code.data,0,"b");
      int findex = lua_gettop(L);
      if(cp->vars.size() > 0) {
        // Passing a closure
        const int sz = cp->vars.size();
        for(int n=0; n < sz;++n) {
          ClosureVar& cv = cp->vars[n];
          if(cv.name == "_ENV") {
            lua_getglobal(L,"_G");
          } else {
            cv.val.unpack(L);
          }
          lua_setupvalue(L,findex,n+1);
          cp->vars.push_back(cv);
        }
        lua_pop(L,lua_gettop(L)-findex);
      }
    } else if(var.which() == empty_t) {
      lua_pushnil(L);
    } else {
      std::cout << "ERROR: Unknown type: " << var.which() << std::endl;
      abort();
    }
  }

  void Holder::pack(lua_State *L,int index) {
    if(lua_isnumber(L,index)) {
      set(lua_tonumber(L,index));
    } else if(lua_isstring(L,index)) {
      set(lua_tostring(L,index));
    } else if(lua_isuserdata(L,index)) {
      lua_pushvalue(L,index);
      int n1 = lua_gettop(L);
      get_mtable(L);
      int n2 = lua_gettop(L);
      assert(n2 == n1 + 1);
      std::string s = lua_tostring(L,-1);
      lua_pop(L,2);
      if(s == future_metatable_name) {
        var = *(future_type *)lua_touserdata(L,index);
      } else if(s == table_metatable_name) {
        var = *(table_ptr *)lua_touserdata(L,index);
      } else if(s == vector_metatable_name) {
        var = *(vector_ptr *)lua_touserdata(L,index);
      } else if(s == locality_metatable_name) {
        var = *(hpx::naming::id_type *)lua_touserdata(L,index);
      } else if(s == lua_client_metatable_name) {
        var = *(lua_aux_client *)lua_touserdata(L,index);
      } else {
        std::cerr << "Can't pack key value!" << lua_type(L,-1) << " s=" << s << std::endl;
        abort();
      }
    } else if(lua_istable(L,index)) {
      try {
        int nn = lua_gettop(L);
        lua_pushvalue(L,index);
        lua_pushnil(L);
        var = table_ptr(new table_inner());
        table_ptr& table = boost::get<table_ptr>(var);
        while(lua_next(L,-2) != 0) {
          lua_pushvalue(L,-2);
          if(lua_isnumber(L,-1)) {
            double key = lua_tonumber(L,-1);
            if(key==table->size+1)
              table->size = key;
            Holder h;
            h.pack(L,-2);
            if(h.var.which() != empty_t) {
              (table->t)[key] = h;
              if(key == 0) {
                std::cout << "pack0:PRINT=" << (*this) << std::endl;
                abort();
              }
            } else {
              std::cout << "pack1:PRINT=" << (*this) << std::endl;
              abort();
            }
          } else if(lua_isstring(L,-1)) {
            const char *keys = lua_tostring(L,-1);
            if(keys == nullptr)
              continue;
            std::string key{keys};
            Holder h;
            h.pack(L,-2);
            if(h.var.which() != empty_t) {
              (table->t)[key] = h;
            } else {
              std::cout << "pack1:PRINT=" << (*this) << std::endl;
              abort();
            }
          } else {
            std::cerr << "Can't pack key value!" << lua_type(L,-1) << std::endl;
            abort();
          }
          lua_pop(L,2);
        }
        if(lua_gettop(L) > nn)
          lua_pop(L,lua_gettop(L)-nn);
        //std::cout << "pack:PRINT=" << (*this) << std::endl;
      } catch(std::exception e) {
        std::cout << "EX=" << e.what() << std::endl;
      }
    } else if(lua_isfunction(L,index)) {
      lua_pushvalue(L,index);
      assert(lua_isfunction(L,-1));
      closure_ptr cp{new Closure()};
      lua_dump(L,(lua_Writer)lua_write,&cp->code.data,true);
      for(int i=1;true;i++) {
        const char *name = lua_getupvalue(L,index,i);
        if(name == 0) break;
        ClosureVar cv;
        cv.name = name;
        if(env != name) {
          cv.val.pack(L,-1);
        } else {
          cv.val.var = Empty();
        }
        cp->vars.push_back(cv);
      }
      var = cp;
      lua_pop(L,1);
    } else if(lua_isnil(L,index)) {
      Empty e;
      var = e;
    } else if(lua_isuserdata(L,index)) {
      std::cout << "Can't pack unknown user data!" << std::endl;
    } else if(lua_isboolean(L,index)) {
      // TODO: Make real support for booleans
      var = lua_toboolean(L,index);
    } else {
      int t = lua_type(L,index);
      std::cerr << "Can't pack value! " << t << std::endl;
      //abort();
    }
  }

inline bool is_bytecode(const std::string& s) {
  return s.size() > 4 && s[0] == 27 && s[1] == 'L' && s[2] == 'u' && s[3] == 'a';
}

LuaEnv::LuaEnv() {
  ptr = get_lua_ptr();
  if(ptr->busy) {
    std::cout << "Busy" << std::endl;
    abort();
  }
  ptr->busy = true;
  L = ptr->get_state();
}
LuaEnv::~LuaEnv() {
  ptr->busy = false;
  set_lua_ptr(ptr);
}
const char *metatables[] = {
  table_metatable_name, table_iter_metatable_name,
  future_metatable_name, guard_metatable_name,
  locality_metatable_name,vector_metatable_name,
  0};

int get_mtable(lua_State *L) {
  if(lua_isnil(L,-1))
    return false;
  if(!lua_isuserdata(L,-1))
    return false;
  int ss1 = lua_gettop(L);
  lua_getfield(L,-1,"Name");
  int ss2 = lua_gettop(L);
  if(ss1+1 != ss2)
    return false;
  if(!lua_iscfunction(L,-1))
    return false;
  lua_CFunction f = lua_tocfunction(L,-1);
  lua_pop(L,1);
  if(f == nullptr)
    return false;
  (*f)(L);
  return lua_gettop(L);
}

bool cmp_meta(lua_State *L,int index,const char *name) {
  if(lua_isnil(L,index))
    return false;
  if(!lua_isuserdata(L,index))
    return false;
  int ss1 = lua_gettop(L);
  lua_getfield(L,index,"Name");
  int ss2 = lua_gettop(L);
  if(ss1+1 != ss2)
    return false;
  if(!lua_iscfunction(L,-1))
    return false;
  lua_CFunction f = lua_tocfunction(L,-1);
  lua_pop(L,1);
  if(f == nullptr)
    return false;
  (*f)(L);
  if(lua_isstring(L,-1)) {
    std::string nm = lua_tostring(L,-1);
    lua_pop(L,1);
    return nm == name;
  }
  return false;
}

guard_type global_guarded{new Guard()};

std::ostream& operator<<(std::ostream& out,const key_type& kt) {
  if(kt.which() == 0)
    out << boost::get<double>(kt) << "{f}";
  else
    out << boost::get<std::string>(kt) << "{s}";
  return out;
}
std::ostream& operator<<(std::ostream& out,const Holder& holder) {
  switch(holder.var.which()) {
    case Holder::empty_t:
      break;
    case Holder::num_t:
      out << boost::get<double>(holder.var) << "{f}";
      break;
    case Holder::str_t:
      out << boost::get<std::string>(holder.var) << "{s}";
      break;
    case Holder::table_t:
      {
        table_ptr t = boost::get<table_ptr>(holder.var);
        out << "{";
        for(auto i=t->t.begin(); i != t->t.end(); ++i) {
          if(i != t->t.begin()) out << ", ";
          out << i->first << ":" << i->second;
        }
        out << "}";
      }
      break;
    case Holder::vector_t:
      {
        vector_ptr t = boost::get<vector_ptr>(holder.var);
        out << "[";
        for(int i=1;i < t->size(); ++i) {
          if(i > 1)
            out << ",";
          out << (*t)[i];
        }
        out << "]";
      }
      break;
    case Holder::fut_t:
      out << "Fut()";
      break;
    case Holder::ptr_t:
      {
        ptr_type p = boost::get<ptr_type>(holder.var);
        out << "{";
        for(auto i=p->begin();i != p->end();++i) {
          if(i != p->begin()) out << ", ";
          out << *i;
        }
        out << "}";
      }
      break;
    default:
      out << "Unk(" << holder.var.which() << ")";
      break;
  }
  return out;
}

//--- Transfer lua bytecode to/from a std:string
int lua_write(lua_State *L,const char *str,unsigned long len,std::string *buf) {
    std::string b(str,len);
    *buf += b;
    return 0;
}

const char *lua_read(lua_State *L,void *data,size_t *size) {
    std::string *rbuf = (std::string*)data;
    (*size) = rbuf->size();
    return rbuf->c_str();
}

//--- Debugging utility, print the Lua stack
std::ostream& show_stack(std::ostream& o,lua_State *L,const char *fname,int line,bool recurse) {
    int n = lua_gettop(L);
    if(!recurse)
      o << "RESTACK:n=" << n << std::endl;
    else
      o << "STACK:n=" << n << " src: " << fname << ":" << line << std::endl;
    for(int i=1;i<=n;i++) {
        if(lua_isnil(L,i)) OUT(i,"nil");
        else if(lua_isnumber(L,i)) OUT2(i,lua_tonumber(L,i),"num");
        else if(lua_isstring(L,i)) OUT2(i,lua_tostring(L,i),"str");
        else if(lua_isboolean(L,i)) OUT2(i,lua_toboolean(L,i),"bool");
        else if(lua_isfunction(L,i)) {
          lua_pushvalue(L,i);
          std::string bytecode;
			    lua_dump(L,(lua_Writer)lua_write,&bytecode,true);
          lua_pop(L,1);
          std::ostringstream msg;
          msg << "function ";
          msg << bytecode.size();
          std::string s = msg.str();
          OUT(i,s.c_str());
        } else if(lua_iscfunction(L,i)) OUT(i,"c-function");
        else if(lua_isthread(L,i)) OUT(i,"thread");
        else if(lua_isuserdata(L,i)) {
          bool found = false;
          /*
          for(int j=0;metatables[j] != nullptr;j++) {
            if(luaL_checkudata(L,i,metatables[j]) != nullptr) {
              OUT(i,metatables[j]);
            }
          }
          */
          int n = lua_gettop(L);
          lua_pushvalue(L,i);
          get_mtable(L);
          std::string s = lua_tostring(L,-1);
          lua_pop(L,lua_gettop(L)-n);
          if(!found)
            OUT(i,s.c_str());
        }
        else if(lua_istable(L,i)) {
          o << i << "] table" << std::endl;
          if(recurse) {
            int nn = lua_gettop(L);
            lua_pushvalue(L,i);
            lua_pushnil(L);
            while(lua_next(L,-2) != 0) {
              lua_pushvalue(L,-2);
              std::string value = "?";
              if(lua_isnumber(L,-2)) {
                value = "num: ";
                value += lua_tostring(L,-2);
              } else if(lua_isstring(L,-2)) {
                value = "string: ";
                value += lua_tostring(L,-2);
              } else if(lua_isboolean(L,-2)) {
                value = "bool: ";
                value += lua_tostring(L,-2);
              } else if(lua_iscfunction(L,-2)) {
                value = "c-function";
              }
              const char *key = lua_tostring(L,-1);
              lua_pop(L,2);
              o << "  key=" << key << " value=" << value << std::endl;
            }
            if(lua_gettop(L) > nn)
              lua_pop(L,lua_gettop(L)-nn);
          }
        } else OUT(i,"other");
    }
    if(!recurse)
      o << "END-RESTACK" << std::endl;
    else
      o << "END-STACK" << std::endl;
    o << std::endl;
    return o;
}

//--- Synchronization for the function registry process
std::map<std::string,std::string> function_registry;

#include <hpx/util/thread_specific_ptr.hpp>
struct lua_interpreter_tag {};
hpx::util::thread_specific_ptr<
    LuaHolder,
    lua_interpreter_tag
> lua_ptr;

//--- Methods for getting/setting the Lua ptr. Ensures
//--- that no two user threads has the same Lua VM.
Lua *get_lua_ptr() {
    LuaHolder *h = lua_ptr.get();
    if(h == nullptr)
      lua_ptr.reset(h = new LuaHolder());
    Lua *lua = h->held;
    if(lua == nullptr) {
        lua = new Lua();
    } else {
      h->held = nullptr;
    }
    lua_State *L = lua->get_state();
    for(auto i=function_registry.begin();i != function_registry.end();++i) {
      // Insert into table
      if(lua_load(L,(lua_Reader)lua_read,(void *)&i->second,i->first.c_str(),"b") != 0) {
        std::cout << "function " << i->first << " size=" << i->second.size() << std::endl;
        SHOW_ERROR(L);
      } else {
        lua_setglobal(L,i->first.c_str());
      }
    }
    return lua;
}

void set_lua_ptr(Lua *lua) {
  LuaHolder *h = lua_ptr.get();
  if(h == nullptr)
    lua_ptr.reset(h = new LuaHolder());
  Lua *l = h->held;
  if(l == nullptr) {
    h->held = lua;
  } else {
    delete lua;
  }
}

//---future data structure---//

int new_future(lua_State *L) {
    size_t nbytes = sizeof(future_type); 
    char *mem = (char *)lua_newuserdata(L, nbytes);
    luaL_setmetatable(L,future_metatable_name);
    new (mem) future_type(); // initialize with placement new
    return 1;
}

int hpx_future_clean(lua_State *L) {
    if(cmp_meta(L,-1,future_metatable_name)) {
      future_type *fnc = (future_type *)lua_touserdata(L,-1);
      dtor(fnc);
    }
    return 1;
}

int hpx_future_get(lua_State *L) {
  if(cmp_meta(L,-1,future_metatable_name)) {
    future_type *fnc = (future_type *)lua_touserdata(L,-1);
    lua_pop(L,1);
    ptr_type result = fnc->get();
    for(auto i=result->begin();i!=result->end();++i) {
      i->unpack(L);
      if(cmp_meta(L,-1,future_metatable_name)) {
        hpx_future_get(L);
      }
    }
    // Need to make sure something is returned
    if(result->size()==0) {
      lua_pushnil(L);
    }
  }
  return lua_gettop(L);
}

ptr_type luax_async2(
    closure_ptr cl,
    ptr_type args);

int luax_wait_all(lua_State *L) {
  int nargs = lua_gettop(L);
  std::vector<future_type> v;
  for(int i=1;i<=nargs;i++) {
    if(lua_istable(L,i) && nargs==1) {
      int top = lua_gettop(L);
      lua_pushvalue(L,i);
      lua_pushnil(L);
      int n = 0;
      while(lua_next(L,-2)) {
        lua_pushvalue(L,-2);
        n++;
        const int ix = -2;
        if(!cmp_meta(L,ix,future_metatable_name)) {
          luai_writestringerror("Argument %d to wait_all is not a future ",n);
          return 0;
        }
        future_type *fnc = (future_type *)lua_touserdata(L,ix);
        v.push_back(*fnc);
        lua_pop(L,2);
      }
      if(lua_gettop(L) > top)
        lua_pop(L,lua_gettop(L)-top);
    } else if(cmp_meta(L,i,future_metatable_name)) {
      future_type *fnc = (future_type *)lua_touserdata(L,i);
      v.push_back(*fnc);
    } else if(cmp_meta(L,i,table_metatable_name)) {
      table_ptr& tp = *(table_ptr *)lua_touserdata(L,i);
      for(auto i=tp->t.begin(); i != tp->t.end(); ++i) {
        int w = i->second.var.which();
        if(w == Holder::fut_t) {
          future_type& f = boost::get<future_type>(i->second.var);
          v.push_back(f);
        }
      }
    }
  }

  new_future(L);
  future_type *fc =
    (future_type *)lua_touserdata(L,-1);

  hpx::wait_all(v);

  return 1;
}

ptr_type luax_when_all2(std::vector<future_type> result) {
  ptr_type pt{new std::vector<Holder>};
  table_ptr t{new table_inner()};
  int n = 1;
  for(auto i=result.begin();i != result.end();++i) {
    Holder h;
    h.var = *i;
    (t->t)[n++] = h;
  }
  Holder h;
  h.var = t;
  pt->push_back(h);

  return pt;
}

int luax_when_all(lua_State *L) {
  int nargs = lua_gettop(L);
  std::vector<future_type> v;
  for(int i=1;i<=nargs;i++) {
    if(lua_istable(L,i) && nargs==1) {
      int top = lua_gettop(L);
      lua_pushvalue(L,i);
      lua_pushnil(L);
      int n = 0;
      while(lua_next(L,-2)) {
        lua_pushvalue(L,-2);
        n++;
        const int ix = -2;
        if(!cmp_meta(L,ix,future_metatable_name)) {
          luai_writestringerror("Argument %d to wait_all() is not a future ",n);
          return 0;
        }
        future_type *fnc = (future_type *)lua_touserdata(L,ix);
        v.push_back(*fnc);
        lua_pop(L,2);
      }
      if(lua_gettop(L) > top)
        lua_pop(L,lua_gettop(L)-top);
    } else if(cmp_meta(L,i,future_metatable_name)) {
      future_type *fnc = (future_type *)lua_touserdata(L,i);
      v.push_back(*fnc);
    }
  }

  new_future(L);
  future_type *fc =
    (future_type *)lua_touserdata(L,-1);

  hpx::shared_future<std::vector<future_type>> result = hpx::when_all(v);
  *fc = result.then(hpx::util::unwrapped(boost::bind(luax_when_all2,_1)));

  return 1;
}

ptr_type get_when_any_result(hpx::when_any_result< std::vector< future_type > > result) {
  ptr_type p{new std::vector<Holder>()};
  //Holder h;
  //h.var = result.index;
  //p->push_back(h);
  table_ptr t{new table_inner()};
  (t->t)["index"].var = result.index+1;
  table_ptr t2{new table_inner()};
  for(int i=0;i<result.futures.size();i++) {
    (t2->t)[i+1].var = result.futures[i];
  }
  (t->t)["futures"].var = t2;
  Holder h;
  h.var = t;
  p->push_back(h);
  return p;
}

int luax_when_any(lua_State *L) {
  int nargs = lua_gettop(L);
  std::vector<future_type> v;
  for(int i=1;i<=nargs;i++) {
    if(lua_istable(L,i) && nargs==1) {
      int top = lua_gettop(L);
      lua_pushvalue(L,i);
      lua_pushnil(L);
      int n = 0;
      while(lua_next(L,-2)) {
        lua_pushvalue(L,-2);
        n++;
        const int ix = -2;
        if(!cmp_meta(L,ix,future_metatable_name)) {
          luai_writestringerror("Argument %d to when_any() is not a future ",n);
          return 0;
        }
        future_type *fnc = (future_type *)lua_touserdata(L,ix);
        v.push_back(*fnc);
        lua_pop(L,2);
      }
      if(lua_gettop(L) > top)
        lua_pop(L,lua_gettop(L)-top);
    } else if(cmp_meta(L,i,future_metatable_name)) {
      future_type *fnc = (future_type *)lua_touserdata(L,i);
      v.push_back(*fnc);
    }
  }

  new_future(L);
  future_type *fc =
    (future_type *)lua_touserdata(L,-1);

  hpx::future< hpx::when_any_result< std::vector< future_type > > > result = hpx::when_any(v);
  *fc = result.then(hpx::util::unwrapped(boost::bind(get_when_any_result,_1)));

  return 1;
}

const char *unwrapped_str = "**unwrapped**";

closure_ptr getfunc(lua_State *L,int index) {
  closure_ptr cl{new Closure};
  if(lua_isstring(L,index)) {
    cl->code.data = lua_tostring(L,index);
  } else if(lua_isfunction(L,index)) {
    lua_pushvalue(L,index);
    int n = lua_gettop(L);
    for(int i=1;true;i++) {
      const char *name = lua_getupvalue(L,n,i);
      if(name == 0) break;
      ClosureVar cv;
      cv.name = name;
      if(env != name) {
        cv.val.pack(L,-1);
      } else {
        cv.val.var = Empty();
      }
      cl->vars.push_back(cv);
    }
    int n2 = lua_gettop(L);
    if(n2 > n) lua_pop(L,n2-n);
    assert(lua_isfunction(L,-1));
    lua_dump(L,(lua_Writer)lua_write,&cl->code.data,true);
    lua_pop(L,1);
  } else if(lua_istable(L,index)) {
    // this is intended to be used with unwrapped
    cl->code.data = unwrapped_str;
  } else {
    STACK;
    cl->code.data = "**error**";
    std::cout << "Getfunc error" << std::endl;
    abort();
  }
  return cl;
}

int hpx_future_then(lua_State *L) {
  if(cmp_meta(L,1,future_metatable_name)) {
    future_type *fnc = (future_type *)lua_touserdata(L,1);
    
    //CHECK_STRING(2,"Future:Then()")

    // Package up the arguments
    ptr_type args(new std::vector<Holder>());
    string_ptr fname{new std::string};
    closure_ptr cl = getfunc(L,2);
    *fname = cl->code.data;
    if(*fname == unwrapped_str) {
      Holder h;
      h.pack(L,1);
      h.push(args);
      *fname = "call";
    }
    int nargs = lua_gettop(L);
    for(int i=3;i<=nargs;i++) {
      Holder h;
      h.pack(L,i);
      h.push(args);
    }
    Holder h;
    h.var = *fnc;
    h.push(args);

    new_future(L);
    future_type *fc =
      (future_type *)lua_touserdata(L,-1);
    *fc = fnc->then(boost::bind(luax_async2,cl,args));
  }
  return 1;
}

int future_name(lua_State *L) {
  lua_pushstring(L,future_metatable_name);
  return 1;
}

int open_future(lua_State *L) {
    static const struct luaL_Reg future_meta_funcs [] = {
        {"Get",&hpx_future_get},
        {"Then",&hpx_future_then},
        {"Name",future_name},
        {NULL,NULL},
    };

    static const struct luaL_Reg future_funcs [] = {
        {"new", &new_future},
        {NULL, NULL}
    };

    luaL_newlib(L,future_funcs);

    luaL_newmetatable(L,future_metatable_name);
    luaL_newlib(L, future_meta_funcs);
    lua_setfield(L,-2,"__index");

    lua_pushstring(L,"__gc");
    lua_pushcfunction(L,hpx_future_clean);
    lua_settable(L,-3);

    lua_pop(L,1);

    return 1;
}

//---guard structure--//

int new_guard(lua_State *L) {
  size_t nbytes = sizeof(guard_type);
  char *guard = (char *)lua_newuserdata(L,nbytes);
  luaL_setmetatable(L,guard_metatable_name);
  new (guard) guard_type(new Guard());
  return 1;
}

int hpx_guard_clean(lua_State *L) {
    if(cmp_meta(L,-1,guard_metatable_name)) {
      guard_type *fnc = (guard_type *)lua_touserdata(L,-1);
      dtor(fnc);
    }
    return 1;
}

int open_guard(lua_State *L) {
    static const struct luaL_Reg guard_meta_funcs [] = {
        {NULL,NULL},
    };

    static const struct luaL_Reg guard_funcs [] = {
        {"new", &new_guard},
        {NULL, NULL}
    };

    luaL_newlib(L,guard_funcs);

    luaL_newmetatable(L,guard_metatable_name);
    luaL_newlib(L, guard_meta_funcs);
    lua_setfield(L,-2,"__index");

    lua_pushstring(L,"__gc");
    lua_pushcfunction(L,hpx_guard_clean);
    lua_settable(L,-3);

    lua_pushstring(L,"name");
    lua_pushstring(L,guard_metatable_name);
    lua_settable(L,-3);
    lua_pop(L,1);

    return 1;
}

//---locality structure--//

int new_locality(lua_State *L) {
  size_t nbytes = sizeof(locality_type);
  char *locality = (char *)lua_newuserdata(L,nbytes);
  luaL_setmetatable(L,locality_metatable_name);
  new (locality) locality_type();
  return 1;
}

int find_here(lua_State *L) {
  new_locality(L);
  locality_type *loc = (locality_type*)lua_touserdata(L,-1);
  *loc = hpx::find_here();
  return 1;
}

int root_locality(lua_State *L) {
  new_locality(L);
  locality_type *loc = (locality_type*)lua_touserdata(L,-1);
  *loc = hpx::find_root_locality();
  return 1;
}

int all_localities(lua_State *L) {
  std::vector<hpx::naming::id_type> all_localities = hpx::find_all_localities();
  lua_createtable(L,all_localities.size(),0); 
  int n = 1;
  for(auto i = all_localities.begin();i != all_localities.end();++i) {
    lua_pushnumber(L,n);
    new_locality(L);
    locality_type *loc = (locality_type*)lua_touserdata(L,-1);
    *loc = *i;
    lua_settable(L,-3);
    n++;
  }
  return 1;
}

int remote_localities(lua_State *L) {
  std::vector<hpx::naming::id_type> remote_localities = hpx::find_all_localities();
  lua_createtable(L,remote_localities.size(),0); 
  int n = 1;
  for(auto i = remote_localities.begin();i != remote_localities.end();++i) {
    lua_pushnumber(L,n);
    new_locality(L);
    locality_type *loc = (locality_type*)lua_touserdata(L,-1);
    *loc = *i;
    lua_settable(L,-3);
    n++;
  }
  return 1;
}

int loc_str(lua_State *L) {
  std::ostringstream msg;
  int n = lua_gettop(L);
  for(int i=1;i<=n;i++) {
    if(lua_isstring(L,i)) {
      msg << lua_tostring(L,i);
    } else if(cmp_meta(L,i,locality_metatable_name)) {
      locality_type *loc = (locality_type*)lua_touserdata(L,i);
      msg << *loc;
    } else {
      return 0;
    }
  }
  lua_pop(L,n);
  lua_pushstring(L,msg.str().c_str());
  return 1;
}

int hpx_locality_clean(lua_State *L) {
    if(cmp_meta(L,-1,locality_metatable_name)) {
      locality_type *fnc = (locality_type *)lua_touserdata(L,-1);
      dtor(fnc);
    }
    return 1;
}

int loc_name(lua_State *L) {
  lua_pushstring(L,locality_metatable_name);
  return 1;
}

int open_hpx(lua_State *L) {
    /*
    static const struct luaL_Reg hpx_meta_funcs [] = {
        {NULL,NULL},
    };
    */

    static const struct luaL_Reg hpx_funcs [] = {
        {"async",async},
        {"start",xlua_start},
        {"stop",xlua_stop},
        {"get_mtable",get_mtable},
        {"discover_counter_types",discover},
        {"get_counter",xlua_get_counter},
        {"get_value",xlua_get_value}, // xxx
        {NULL, NULL}
    };

    luaL_newlib(L,hpx_funcs);

    return 1;
}

int open_locality(lua_State *L) {
    static const struct luaL_Reg locality_meta_funcs [] = {
        {"str",&loc_str},
        {"Name",&loc_name},
        {NULL,NULL},
    };

    static const struct luaL_Reg locality_funcs [] = {
        {"new", &new_locality},
        {NULL, NULL}
    };

    luaL_newlib(L,locality_funcs);

    luaL_newmetatable(L,locality_metatable_name);
    luaL_newlib(L, locality_meta_funcs);
    lua_setfield(L,-2,"__index");

    lua_pushstring(L,"__gc");
    lua_pushcfunction(L,hpx_locality_clean);
    lua_settable(L,-3);

    lua_pushstring(L,"__concat");
    lua_pushcfunction(L,loc_str);
    lua_settable(L,-3);

    lua_pop(L,1);

    return 1;
}

//--- Alternative implementation of when_all
#define WHEN_ALL hpx::when_all
//#define WHEN_ALL my_when_all

template<typename Future>
void my_when_all2(
    hpx::lcos::local::promise<std::vector<Future> > *pr,
    std::vector<Future>& futs,
    int index)
{
  const int n = futs.size();
  for(;index < n;index++) {
    if(!futs[index].is_ready()) {
      auto shared_state = hpx::traits::detail::get_shared_state(futs[index]);
      auto f = hpx::util::bind(my_when_all2<Future>,pr,futs,index+1);
      shared_state->set_on_completed(f);
      return;
    }
  }
  pr->set_value(futs);
  delete pr;
}

template<typename Future>
hpx::future<std::vector<Future> > my_when_all(
    std::vector<Future>& futs)
{
  const int n = futs.size();
  for(int index=0;index < n;index++) {
    if(!futs[index].is_ready()) {
      auto p = new hpx::lcos::local::promise<std::vector<Future> >();
      auto fut = p->get_future();
      auto shared_state = hpx::traits::detail::get_shared_state(futs[index]);
      auto f = hpx::util::bind(my_when_all2<Future>,p,futs,index+1);
      shared_state->set_on_completed(f);
      return fut;
    }
  }
  return make_ready_future(futs);
}

template<typename Future>
hpx::future<std::vector<Future> > my_when_all(
    std::vector<Future>&& futs_)
{
  std::vector<Future> futs;
  futs.swap(futs_);
  const int n = futs.size();
  for(int index=0;index < n;index++) {
    if(!futs[index].is_ready()) {
      auto p = new hpx::lcos::local::promise<std::vector<Future> >();
      auto fut = p->get_future();
      auto shared_state = hpx::traits::detail::get_shared_state(futs[index]);
      auto f = hpx::util::bind(my_when_all2<Future>,p,futs,index+1);
      shared_state->set_on_completed(f);
      return fut;
    }
  }
  return make_ready_future(futs);
}

//--- Use these methods to process function inputs
std::shared_ptr<std::vector<ptr_type> > realize_when_all_inputs_step2(ptr_type args,std::vector<future_type> results) {
  std::shared_ptr<std::vector<ptr_type> > results_step2(new std::vector<ptr_type>());
  for(auto i=results.begin();i != results.end();++i) {
    results_step2->push_back(i->get());
  }
  #if 0
  std::vector<future_type> futs;
  for(auto j=results_step2->begin();j != results_step2->end();++j) {
    ptr_type& p = *j;
    for(auto i=p->begin();i != p->end();++i) {
      if(i->var.which() == Holder::fut_t) {
        futs.push_back(boost::get<future_type>(i->var));
      }
    }
  }
  if(futs.size() == 0)
  hpx::future<std::vector<future_type> > result = WHEN_ALL(futs);
  return result.then(hpx::util::unwrapped(boost::bind(realize_when_all_inputs_step2,args,_1)));
  #endif
  return results_step2;
}

hpx::future<std::shared_ptr<std::vector<ptr_type> > > realize_when_all_inputs(ptr_type args) {
  std::vector<future_type> futs;
  for(auto i=args->begin();i != args->end();++i) {
    int w = i->var.which();
    if(w == Holder::fut_t) {
      futs.push_back(boost::get<future_type>(i->var));
    }
  }
  hpx::future<std::vector<future_type> > result = WHEN_ALL(futs);
  return result.then(hpx::util::unwrapped(boost::bind(realize_when_all_inputs_step2,args,_1)));
}

//--- Use these methods to process function outputs
ptr_type realize_when_all_outputs_step2(ptr_type args,std::vector<future_type> results) {
  auto j = results.begin();
  for(auto i=args->begin();i != args->end();++i) {
    int w = i->var.which();
    if(w == Holder::fut_t) {
      i->var = j->get();
      j++;
    }
  }
  return args;
}

future_type realize_when_all_outputs(ptr_type args) {
  std::vector<future_type> futs;
  for(auto i=args->begin();i != args->end();++i) {
    int w = i->var.which();
    if(w == Holder::fut_t) {
      futs.push_back(boost::get<future_type>(i->var));
    }
  }
  hpx::future<std::vector<future_type> > result = WHEN_ALL(std::move(futs));
  return result.then(hpx::util::unwrapped(boost::bind(realize_when_all_outputs_step2,args,_1)));
}

int xlua_unwrapped(lua_State *L) {
  int n = lua_gettop(L);
  lua_createtable(L,0,2);
  lua_pushstring(L,"func");
  lua_pushvalue(L,1);
  lua_settable(L,-3);
  lua_remove(L,1);
  lua_pushstring(L,"args");
  lua_createtable(L,n,0);
  for(int i=1;i<n;i++) {
    lua_pushnumber(L,i);
    lua_pushvalue(L,1);
    lua_settable(L,-3);
    lua_remove(L,1);
  }
  lua_settable(L,-3);
  lua_pushstring(L,"unwrapped");
  lua_pushboolean(L,true);
  lua_settable(L,-3);
  return 1;
}

bool loadFunc(lua_State *L) {
  if(lua_isfunction(L,-1)) {
    lua_insert(L,1);
    return true;
  } else if(lua_isstring(L,-1)) {
    const char *func = lua_tostring(L,-1);
    /*
    if(is_bytecode(func)) {
      std::string bytecode = func;
      if(lua_load(L,(lua_Reader)lua_read,(void *)&bytecode,0,"b") != 0) {
        lua_pop(L,1);
      }
      STACK;
      return true;
    }
    */
    // TODO FIX
    #if 0
    auto search = globals->t.find(func);
    if(search != globals->t.end() && search->second.var.which() == Holder::bytecode_t) {
      Bytecode bytecode = boost::get<Bytecode>(search->second.var);
      if(lua_load(L,(lua_Reader)lua_read,(void *)&bytecode.data,0,"b") != 0) {
        lua_pop(L,1);
        return true;
      }
    }
    #endif
    lua_getglobal(L,func);
    if(lua_isfunction(L,-1)) {
      lua_insert(L,1);
      lua_pop(L,1);
      return true;
    } else {
      std::string msg = func;
      msg += " is not a function";
      lua_pushstring(L,msg.c_str());
      return false;
    }
  } else {
    lua_pushstring(L,"No function supplied to call");
    return false;
  }
}

int call(lua_State *L) {
  int argn = 1;
  if(cmp_meta(L,1,table_metatable_name)) {
    table_ptr& tp = *(table_ptr *)lua_touserdata(L,-1);
    Holder hfunc = (tp->t)["func"];
    hfunc.unpack(L);
    if(!loadFunc(L))
      return 0;
    Holder hargs = (tp->t)["args"];
    table_ptr tpargs = boost::get<table_ptr>(hargs.var);
    for(int i=1;i<=tpargs->size;i++) {
      (tpargs->t)[i].unpack(L);
      while(cmp_meta(L,-1,future_metatable_name)) {
        future_type *fc =
          (future_type *)lua_touserdata(L,-1);
        ptr_type p = fc->get();
        for(int i=0;i<p->size();i++) {
          (*p)[i].unpack(L);
          lua_remove(L,-2);
        }
      }
    }
    lua_remove(L,2);
    argn = lua_gettop(L);
  } else {
    std::string func;
    if(lua_istable(L,-1)) {
      // Func
      lua_pushstring(L,"func");
      lua_gettable(L,-2);
      if(!loadFunc(L))
        return 0;
      // Args
      lua_pushstring(L,"args");
      lua_gettable(L,-2);
      if(lua_istable(L,-1)) {
        lua_pushnil(L);
        while(lua_next(L,-2) != 0) {
          while(cmp_meta(L,-1,future_metatable_name)) {
            future_type *fc =
              (future_type *)lua_touserdata(L,-1);
            ptr_type p = fc->get();
            for(int i=0;i<p->size();i++) {
              (*p)[i].unpack(L);
              lua_remove(L,-2);
            }
          }
          lua_insert(L,++argn);
          lua_pushvalue(L,-1);
          lua_remove(L,-2);
        }
      } else {
        lua_pushstring(L,"args is not a table");
        return 0;
      }
    }
    lua_remove(L,-1);
    lua_remove(L,-1);
  }
  lua_pcall(L,argn-1,max_output_args,0);
  return lua_gettop(L);
}

//--- Handle dataflow calling from Lua
ptr_type luax_dataflow2(
    string_ptr fname,
    ptr_type args,
    std::shared_ptr<std::vector<ptr_type> > futs) {
  ptr_type answers(new std::vector<Holder>());

  {
    LuaEnv lenv;

    lua_State *L = lenv.get_state();

    bool found = false;

    lua_pop(L,lua_gettop(L));

    if(is_bytecode(*fname)) {
      if(lua_load(L,(lua_Reader)lua_read,(void *)fname.get(),0,"b") != 0) {
        std::cout << "Error in function: size=" << fname->size() << std::endl;
        SHOW_ERROR(L);
      }
    } else {
      lua_getglobal(L,fname->c_str());
      if(lua_isfunction(L,-1)) {
        found = true;
      }
      #if 0
      if(!found) {
        auto search = globals->t.find(*fname);
        if(search != globals->t.end() && search->second.var.which() == Holder::bytecode_t) {
          Bytecode bytecode = boost::get<Bytecode>(search->second.var);
          if(lua_load(L,(lua_Reader)lua_read,(void *)&bytecode.data,0,"b") != 0) {
            found = true;
          }
        }
      }
      #endif

      if(!found) {
        if(function_registry.find(*fname) == function_registry.end()) {
          std::cout << "Function '" << *fname << "' is not defined(3)." << std::endl;
          return answers;
        }

        std::string bytecode = function_registry[*fname];
        if(lua_load(L,(lua_Reader)lua_read,(void *)&bytecode,fname->c_str(),"b") != 0) {
          std::cout << "Error in function: '" << *fname << "' size=" << bytecode.size() << std::endl;
          SHOW_ERROR(L);
          return answers;
        }

        lua_setglobal(L,fname->c_str());
      }
    }

    // Push data from the concrete values and ready futures onto the Lua stack
    auto f = futs->begin();
    for(auto i=args->begin();i!=args->end();++i) {
      int w = i->var.which();
      if(w == Holder::fut_t) {
        for(auto j=(*f)->begin();j != (*f)->end();++j)
          j->unpack(L);
        f++;
      } else {
        i->unpack(L);
      }
    }

    lua_getglobal(L,fname->c_str());
    lua_insert(L,1);

    //std::ostringstream msg;
    //show_stack(msg,L,__LINE__);
    // Provide a maximum number output args
    if(lua_pcall(L,args->size(),max_output_args,0) != 0) {
      SHOW_ERROR(L);
      return answers;
    }

    // Trim stack
    int nargs = lua_gettop(L);
    while(nargs > 0 && lua_isnil(L,-1)) {
      lua_pop(L,1);
      nargs--;
    }

    for(int i=1;i<=nargs;i++) {
      Holder h;
      h.pack(L,i);
      h.push(answers);
    }
    lua_pop(L,nargs);
  }

  return answers;
}

//--- Handle async calling from Lua
ptr_type luax_async2(
    closure_ptr cl,
    ptr_type args) {
  ptr_type answers(new std::vector<Holder>());

  {
    LuaEnv lenv;

    lua_State *L = lenv.get_state();

    bool found = false;

    lua_pop(L,lua_gettop(L));

    if(is_bytecode(cl->code.data)) {
      if(lua_load(L,(lua_Reader)lua_read,(void *)&cl->code.data,0,"b") != 0) {
        std::cout << "Error in function: size=" << cl->code.data.size() << std::endl;
        SHOW_ERROR(L);
      } else {
        int findex = lua_gettop(L);
        if(cl->vars.size() > 0) {
          // Passing a closure
          const int sz = cl->vars.size();
          for(int n=0; n < sz;++n) {
            ClosureVar& cv = cl->vars[n];
            if(cv.name == "_ENV") {
              lua_getglobal(L,"_G");
            } else {
              cv.val.unpack(L);
            }
            lua_setupvalue(L,findex,n+1);
            cl->vars.push_back(cv);
          }
          lua_pop(L,lua_gettop(L)-findex);
        }
      }
    } else {
      lua_getglobal(L,cl->code.data.c_str());
      if(lua_isfunction(L,-1)) {
        found = true;
      } else {
        lua_pop(L,1);
      }
      if(!found) {
        std::string skey = cl->code.data + "{s}";
        auto search = globals->t.find(cl->code.data);
        if(search != globals->t.end()) {
          if(search->second.var.which() == Holder::bytecode_t) {
            Bytecode bytecode = boost::get<Bytecode>(search->second.var);
            int rc = lua_load(L,(lua_Reader)lua_read,(void *)&bytecode.data,0,"b");
            if(rc == LUA_OK) {
              found = true;
            } else {
              SHOW_ERROR(L);
            }
          }
        }
      }

      if(!found) {
        if(function_registry.find(cl->code.data) == function_registry.end()) {
          std::cout << "Function '" << cl->code.data << "' is not defined." << std::endl;
          return answers;
        }

        std::string bytecode = function_registry[cl->code.data];
        if(lua_load(L,(lua_Reader)lua_read,(void *)&bytecode,cl->code.data.c_str(),"b") != 0) {
          std::cout << "Error in function: '" << cl->code.data << "' size=" << bytecode.size() << std::endl;
          SHOW_ERROR(L);
          return answers;
        }

        lua_setglobal(L,cl->code.data.c_str());
        lua_getglobal(L,cl->code.data.c_str());
      }
    }

    // Push data from the concrete values and ready futures onto the Lua stack
    for(auto i=args->begin();i!=args->end();++i) {
      i->unpack(L);
    }

    const int max_output_args = 10;
    if(lua_pcall(L,args->size(),max_output_args,0) != 0) {
      //std::cout << msg.str();
      SHOW_ERROR(L);
      return answers;
    }

    // Trim stack
    int nargs = lua_gettop(L);
    while(nargs > 0 && lua_isnil(L,-1)) {
      lua_pop(L,1);
      nargs--;
    }

    for(int i=1;i<=nargs;i++) {
      Holder h;
      h.pack(L,i);
      h.push(answers);
    }
    lua_pop(L,nargs);
  }

  return answers;
}

//--- Realize futures in inputs, call dataflow function, realize futures in outputs
future_type luax_dataflow(
    string_ptr fname,
    ptr_type args) {
    // wait for all futures in input
    hpx::future<std::shared_ptr<std::vector<ptr_type> > > f1 = realize_when_all_inputs(args);
    // pass values of all futures along with args
    future_type f2 = f1.then(hpx::util::unwrapped(boost::bind(luax_dataflow2,fname,args,_1)));
    // clean all futures out of returns
    return f2.then(hpx::util::unwrapped(boost::bind(realize_when_all_outputs,_1)));
}

int remote_reg(std::map<std::string,std::string> registry);

}

HPX_PLAIN_ACTION(hpx::luax_dataflow,luax_dataflow_action);
HPX_PLAIN_ACTION(hpx::luax_async2,luax_async_action);
HPX_PLAIN_ACTION(hpx::remote_reg,remote_reg_action);
HPX_REGISTER_BROADCAST_ACTION_DECLARATION(remote_reg_action);
HPX_REGISTER_BROADCAST_ACTION(remote_reg_action);

namespace hpx {

int luax_run_guarded(lua_State *L) {
  int n = lua_gettop(L);
  CHECK_STRING(-1,"run_guarded")
  string_ptr fname(new std::string(lua_tostring(L,-1)));
  guard_type g;
  if(n == 1) {
    g = global_guarded;
  } else if(n == 2) {
    g = *(guard_type *)lua_touserdata(L,-2);
  } else if(n > 2) {
    std::shared_ptr<hpx::lcos::local::guard_set> gs{new hpx::lcos::local::guard_set()};
    ptr_type all_data{new std::vector<Holder>()};

    guard_type *gv = new guard_type[n];
    for(int i=1;i<n;i++) {
      guard_type g2 = *(guard_type *)lua_touserdata(L,i);
      gv[i-1]=g2;
      gs->add(g2->g);
      Holder h;
      h.var = g2->g_data;
      all_data->push_back(h);
    }
    boost::function<void()> func = boost::bind(hpx_srun,fname,all_data,gv,n);
    run_guarded(*gs,func);
    return 1;
  }
  lua_pop(L,n);
  guard_type *gv = new guard_type[1];
  gv[0] = g;
  boost::function<void()> func = boost::bind(hpx_srun,fname,g->g_data,gv,1);
  run_guarded(*g->g,func);
  return 1;
}

int isfuture(lua_State *L) {
    if(cmp_meta(L,-1,future_metatable_name)) {
      lua_pop(L,1);
      lua_pushboolean(L,1);
    } else {
      lua_pop(L,1);
      lua_pushboolean(L,0);
    }
    return 1;
}

int isvector(lua_State *L) {
    if(cmp_meta(L,-1,vector_metatable_name)) {
      lua_pop(L,1);
      lua_pushboolean(L,1);
    } else {
      lua_pop(L,1);
      lua_pushboolean(L,0);
    }
    return 1;
}

int istable(lua_State *L) {
    if(cmp_meta(L,-1,table_metatable_name)) {
      lua_pop(L,1);
      lua_pushboolean(L,1);
    } else {
      lua_pop(L,1);
      lua_pushboolean(L,0);
    }
    return 1;
}

int islocality(lua_State *L) {
    if(cmp_meta(L,-1,locality_metatable_name)) {
      lua_pop(L,1);
      lua_pushboolean(L,1);
    } else {
      lua_pop(L,1);
      lua_pushboolean(L,0);
    }
    return 1;
}

int dataflow(lua_State *L) {

    locality_type *loc = nullptr;
    if(cmp_meta(L,1,locality_metatable_name)) {
      loc = (locality_type *)lua_touserdata(L,1);
      lua_remove(L,1);
    }

    // Package up the arguments
    ptr_type args(new std::vector<Holder>());
    int nargs = lua_gettop(L);
    for(int i=2;i<=nargs;i++) {
      Holder h;
      h.pack(L,i);
      h.push(args);
    }
    
    string_ptr fname(new std::string);
    closure_ptr cl = getfunc(L,1);
    *fname = cl->code.data;
    if(*fname == unwrapped_str) {
      Holder h;
      h.pack(L,1);
      h.push(args);
      *fname = "call";
    }

    // Launch the thread
    future_type f =
      (loc == nullptr) ?
        hpx::async(luax_dataflow,fname,args) :
        hpx::async<luax_dataflow_action>(*loc,fname,args);

    new_future(L);
    future_type *fc =
      (future_type *)lua_touserdata(L,-1);
    *fc = f;
    return 1;
}

int async(lua_State *L) {

    locality_type *loc = nullptr;
    if(cmp_meta(L,1,locality_metatable_name)) {
      loc = (locality_type *)lua_touserdata(L,1);
      lua_remove(L,1);
    }

    // Package up the arguments
    ptr_type args(new std::vector<Holder>());
    int nargs = lua_gettop(L);
    
    //CHECK_STRING(1,"async")
    closure_ptr cl = getfunc(L,1);
    if(cl->code.data == unwrapped_str) {
      Holder h;
      h.pack(L,1);
      h.push(args);
      cl->code.data = "call";
    }
    for(int i=2;i<=nargs;i++) {
      Holder h;
      h.pack(L,i);
      h.push(args);
    }

    // Launch the thread
    future_type f =
      (loc == nullptr) ?
        hpx::async(luax_async2,cl,args) :
        hpx::async<luax_async_action>(*loc,cl,args);

    new_future(L);
    future_type *fc =
      (future_type *)lua_touserdata(L,-1);
    *fc = f;
    return 1;
}

void unwrap_future(lua_State *L,int index,future_type& f) {
  ptr_type p = f.get();
  if(p->size() == 1) {
    if((*p)[0].var.which() == Holder::fut_t) {
      future_type& f2 = boost::get<future_type>((*p)[0].var);
      unwrap_future(L,index,f2);
    } else {
      (*p)[0].unpack(L);
      lua_replace(L,index);
    }
  }
}

int unwrap(lua_State *L) {
    int nargs = lua_gettop(L);
    for(int i=1;i<=nargs;i++) {
      if(cmp_meta(L,i,future_metatable_name) ) {
        future_type *fc = (future_type *)lua_touserdata(L,i);
        unwrap_future(L,i,*fc);
      }
    }
    return 1;
}

int remote_reg(std::map<std::string,std::string> registry) {
	LuaEnv lenv;
    lua_State *L = lenv.get_state();
	function_registry = registry;
	for(auto i = registry.begin();i != registry.end();++i) {
		std::string& bytecode = i->second;
		if(lua_load(L,(lua_Reader)lua_read,(void *)&bytecode,i->first.c_str(),"b") != 0) {
			std::cout << "Error in function: " << i->first << " size=" << bytecode.size() << std::endl;
			SHOW_ERROR(L);
			return -1;
		}

		lua_setglobal(L,i->first.c_str());
	}
	return 0;
}

// TODO: add wrappers to conveniently get and use tables?

// TODO: Extend to include loading of libraries from .so files and running scripts.
// Example: hpx_reg('init.lua','power','pr()')
// The first is a script, the second a lib (named either power.so or libpower.so),
// the third a function. 
int hpx_reg(lua_State *L) {
	while(lua_gettop(L)>0) {
    CHECK_STRING(-1,"HPX_PLAIN_ACTION")
		if(lua_isstring(L,-1)) {
			const int n = lua_gettop(L);
			std::string fname = lua_tostring(L,-1);
			lua_getglobal(L,fname.c_str());
      Bytecode bc;
			lua_dump(L,(lua_Writer)lua_write,&bc.data,true);
			function_registry[fname]=bc.data;
      (globals->t)[fname].var = bc;
			//std::cout << "register(" << fname << "):size=" << bytecode.size() << std::endl;
			const int nf = lua_gettop(L);
			if(nf > n) {
				lua_pop(L,nf-n);
			}
		}
		lua_pop(L,1);
	}

	std::vector<hpx::naming::id_type> remote_localities = hpx::find_remote_localities();
  if(remote_localities.size() > 0) {
    auto f = hpx::lcos::broadcast<remote_reg_action>(remote_localities,function_registry);
    f.get(); // in case there are exceptions
  }
  
	return 1;
}

void hpx_srun(string_ptr fname,ptr_type gdata,guard_type *gv,int ng) {
    LuaEnv lenv;
    lua_State *L = lenv.get_state();
    int n = lua_gettop(L);
    lua_pop(L,n);
    for(auto i=gdata->begin();i!=gdata->end();++i) {
      i->unpack(L);
    }
    hpx_srun(L,*fname,gdata);
    gdata->clear();

    // Trim stack
    int nargs = lua_gettop(L);
    while(nargs > 0 && lua_isnil(L,-1)) {
      lua_pop(L,1);
      nargs--;
    }

    for(int i=1;i<=nargs;i++) {
      Holder h;
      h.pack(L,i);
      guard_type g;
      int n = i <= ng ? i-1 : ng-1;
      g = gv[n];
      g->g_data->clear();
      h.push(g->g_data);
    }
    delete[] gv;
}

int hpx_run(lua_State *L) {
  CHECK_STRING(1,"hpx_run")
  std::string fname = lua_tostring(L,1);
  lua_remove(L,1);
  return hpx_srun(L,fname,global_guarded->g_data);
}

int hpx_srun(lua_State *L,std::string& fname,ptr_type gdata) {
  int n = lua_gettop(L);
  if(function_registry.find(fname) == function_registry.end()) {
    std::cout << "Function '" << fname << "' is not defined(2)." << std::endl;
    return 0;
  }

  std::string bytecode = function_registry[fname];
  if(lua_load(L,(lua_Reader)lua_read,(void *)&bytecode,0,"b") != 0) {
    std::cout << "Error in function: " << fname << " size=" << bytecode.size() << std::endl;
    SHOW_ERROR(L);
    return 0;
  }

  if(!lua_isfunction(L,-1)) {
    std::cout << "Failed to load byte code for " << fname << std::endl;
    return 0;
  }

  lua_insert(L,1);
  if(lua_pcall(L,n,10,0) != 0) {
    SHOW_ERROR(L);
    return 0;
  }
  return 1;
}

int make_ready_future(lua_State *L) {
  ptr_type pt{new std::vector<Holder>};
  int nargs = lua_gettop(L);
  for(int i=1;i<=nargs;i++) {
    Holder h;
    h.pack(L,i);
    h.push(pt);
  }
  lua_pop(L,nargs);
  new_future(L);
  future_type *fc =
    (future_type *)lua_touserdata(L,-1);
  *fc = hpx::make_ready_future(pt);
  return 1;
}

}
