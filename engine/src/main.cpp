#include <iostream>
#include "headers/SlabAllocator.h"
#include "headers/FlatTable.h"
#include <cassert>
#include <cstring>

using namespace std;

int main()
{
    // 1. Slab gerenciando blocos de 128 bytes (capacidade de 10.000 itens)
    SlabAllocator slab128(128, 10000);

    // 2. Flat Table contígua com 16.384 slots (potência de 2 para bitmasking rápido)
    FlatTable cache(16384, slab128);

    // Gravação básica (SET)
    cache.set("session:token:usr_99", "eyJhbGciOiJIUzI1NiJ9");
    cache.set("metrics:db:query_ms", 14.2);

    // Leitura (GET)
    if (auto val = cache.get("session:token:usr_99"))
    {
        cout << "GET session:token:usr_99 -> " << *val << "\n";
    }

    if (auto data = cache.get("metrics:db:query_ms"))
    {
        cout << "GET metrics:db:query_ms -> " << *data << "\n";
    }

    // === TESTES DE TIPOS (VALUE) ===
    cout << "\n=== TESTES DE TIPOS ===\n";

    // 1. STRING
    cache.set("type:string", "hello world");
    auto resStr = cache.get("type:string");
    assert(resStr.has_value());
    assert(resStr->type == ValueType::STRING);
    assert(resStr->asString() == "hello world");
    cout << "[STRING]  type:string  -> " << *resStr << "\n";

    // 2. INTEGER (int64_t)
    int64_t intVal = 1234567890LL;
    cache.set("type:int", intVal);
    auto resInt = cache.get("type:int");
    assert(resInt.has_value());
    assert(resInt->type == ValueType::INTEGER);
    assert(resInt->asInt() == intVal);
    cout << "[INTEGER] type:int     -> " << *resInt << "\n";

    // 3. FLOAT (double)
    double dblVal = 3.1415926535;
    cache.set("type:float", dblVal);
    auto resFloat = cache.get("type:float");
    assert(resFloat.has_value());
    assert(resFloat->type == ValueType::FLOAT);
    assert(resFloat->asFloat() == dblVal);
    cout << "[FLOAT]   type:float   -> " << *resFloat << "\n";

    // 4. JSON
    cache.set("type:json", Json{R"({"active": true, "count": 42})"});
    auto resJson = cache.get("type:json");
    assert(resJson.has_value());
    assert(resJson->type == ValueType::JSON);
    assert(resJson->asString() == R"({"active": true, "count": 42})");
    cout << "[JSON]    type:json    -> " << *resJson << "\n";

    // 5. BINARY
    Binary binData = {0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02};
    cache.set("type:binary", binData);
    auto resBin = cache.get("type:binary");
    assert(resBin.has_value());
    assert(resBin->type == ValueType::BINARY);
    assert(resBin->bytes.size() == binData.size());
    assert(memcmp(resBin->bytes.data(), binData.data(), binData.size()) == 0);
    cout << "[BINARY]  type:binary  -> " << resBin->bytes.size() << " bytes gravados e verificados com sucesso\n";

    cout << "\n=== TESTES DE DOUBLE FREE ===\n";
    void *a = slab128.allocate();
    assert(a != nullptr);

    assert(slab128.deallocate(a));  // true
    assert(!slab128.deallocate(a)); // false: double-free

    void *b = slab128.allocate();
    // b pode reutilizar o endereço de a.
    assert(b == a);
    assert(slab128.deallocate(b));
    cout << "Double free verificado com sucesso!\n";

    return 0;
}
