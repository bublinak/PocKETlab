# Vítejte v dokumentaci PocKETlab

PocKETlab je projekt bakalářské práce Jiřího Švihly, jehož cílem je návrh jednoduchého charakterografu kapesní velikosti.

## Navigace

*   [Specifikace cílových požadavků na zařízení](Specifikace-pozadavku)
*   [Koncepce hardwarové části](Koncepce-hardware)
*   [Uživatelská aplikace](Uzivatelska-aplikace)
*   [Seznam symbolů a zkratek](Seznam-symbolu-a-zkratek)

## Abstrakt práce

Tato práce se zabývá návrhem jednoduchého charakterografu kapesní velikosti určeného k měření V-A, frekvenčních a dalších charakteristik na aktivních i pasivních dvojbranech (a jim podobným obvodům).
V rámci udržení jednoduchosti návrhu a přijatelné uživatelské přívětivosti se práce omezuje na nízkofrekvenční aplikace a provoz na malém napětí.
Zařízení je založeno na platformě ESP32-S3 (4MB Flash + 2MB PSRAM), napájeno pomocí USB Power Delivery 3.2 (do 20V@60W s podporou PPS) a je vybaveno 12-bit A/D a D/A převodníky s rychlostí 100ks/s. Pro měření V-A a zatěžovacích charakteristik je vybaveno napájecím i zátěžovým kanálem s možností lineární i pulzní regulace.
