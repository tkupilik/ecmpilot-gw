# Karta auta pro Home Assistant

[`vehicle-card.yaml`](vehicle-card.yaml) je obsah **jedné karty** do editoru dashboardu. Obsahuje SOC, dojezd, okolní teplotu, SOC 12V baterie, předehřev, nabíjení a zámky. Nahoře ukazuje dostupnost dat GW, hlášený stav BSI a stáří SOC. Karta je pouze pro čtení, bez odkazů a bez akcí při kliknutí.

## Vložení

1. Dashboard → Upravit → Přidat kartu → Ruční / Manual.
2. Smaž výchozí `type: ''` a vlož **celý obsah** `vehicle-card.yaml`.
3. Zkontroluj náhled a ulož kartu.

Kartu lze přidat do existující sekce dashboardu. YAML je konfigurace karty, ne celé sekce nebo celého dashboardu. Není potřeba měnit `configuration.yaml`, restartovat HA ani instalovat HACS.

Používá vestavěnou [Markdown kartu](https://www.home-assistant.io/dashboards/markdown/), šablony Jinja, povolené HTML tabulky a ikony HA. Vzhled vychází z telefonu; SOC se zobrazuje číselně, bez bargrafu. Ventilátor je statická ikona. Karta nevyžaduje card-mod. Barvy pozadí a běžného textu přebírá z motivu HA; běžné hodnoty nemají vynucenou barvu. Řádek BSI a stáří i vypnuté funkce jsou šedé. Zelená zvýrazňuje aktivní nabíjení/předehřev a malý indikátor GW online; odemčení a chyba nabíjení jsou oranžové.

## Entity

SOC `sensor.ecmpilot_gateway_battery_state_of_charge` je potvrzený uživatelem. Ostatní ID jsou odvozená ze stejného prefixu a aktuálních discovery názvů v `main/ha_telemetry.c`. Pokud byly v HA přejmenované nebo dostaly příponu `_2`, oprav příslušný řádek v bloku `set e = dict(...)` na začátku `content`. Chybějící entity zobrazují `—` nebo „Nedostupné“.

| Údaj | Předpokládané entity_id |
|---|---|
| SOC | `sensor.ecmpilot_gateway_battery_state_of_charge` |
| Dojezd | `sensor.ecmpilot_gateway_estimated_range` |
| Okolní teplota | `sensor.ecmpilot_gateway_outside_temperature` |
| SOC 12V | `sensor.ecmpilot_gateway_12v_battery_state_of_charge` |
| BSI | `sensor.ecmpilot_gateway_bsi_state` |
| Předehřev | `sensor.ecmpilot_gateway_reported_preconditioning_state` |
| Nabíjení | `sensor.ecmpilot_gateway_charging_state` |
| Režim nabíjení | `sensor.ecmpilot_gateway_charging_mode` |
| Kabel | `binary_sensor.ecmpilot_gateway_charging_cable` |
| Čas nabíjení | `sensor.ecmpilot_gateway_delayed_charging_start` |
| Zámky | `sensor.ecmpilot_gateway_vehicle_locking_state` |
| Diagnostika stáří | `sensor.ecmpilot_gateway_battery_data_age` |
| Dostupnost GW (místní nastavení) | `binary_sensor.ecmpilot_gateway_charge_limit_enabled` |

## Stáří a barvy

Každý údaj používá své stáří z mapy **`age_s` na jediném diagnostickém senzoru Battery data age**, ne dobu poslední změny hodnoty v HA. GW posílá snímek jednou za sekundu, pokud přijala zprávu BSI 036 během posledních 2 sekund; při tichu jednou za minutu. Rozhoduje skutečný příjem bez ohledu na obsah power režimu, nikoliv změna hodnoty nebo Force. Atribut `received_at` vytvoří HA při přijetí snímku; karta od něj místně dopočítává uběhlý čas. Čerstvé BSI neomlazuje SOC ani ostatní údaje. Není potřeba synchronizovaný čas GW. Hodnoty starší než 5 minut zůstávají viditelné a zesednou. Úplně nedostupná MQTT entita zobrazí pomlčku; karta nevytváří vlastní trvalou cache pro odpojenou GW.

Nabíjení má zelenou barvu při čerstvém stavu `charging`, modrou pro `delayed`. Čerstvé odpojení kabelu ruší zobrazení odloženého režimu. Předehřev a zámky používají vlastní stáří z diagnostické mapy. Chybějící stáří nebo čas přijetí znamená neověřenou čerstvost a šedé zobrazení. Po obnovení příjmu BSI diagnostika přejde na rychlé odesílání bez čekání na původní minutový termín. Hodnoty samotné se aktualizují podle intervalu GW.

## Diagnostika bez historie

[`recorder-exclude.yaml`](recorder-exclude.yaml) obsahuje výjimku pro `sensor.ecmpilot_gateway_battery_data_age`. Přidej ji do `configuration.yaml`; pokud už existuje sekce `recorder`, sluč její `exclude.entities` s tímto seznamem a nevytvářej druhou sekci. Ověř skutečné ID diagnostického senzoru, zejména pokud byl přejmenovaný. Poté restartuj HA. Pokud je senzor výslovně uvedený i v `recorder.include.entities`, odstraň jej z tohoto seznamu, protože explicitní include má přednost.

Označení `diagnostic` samo historii nevypíná. Výjimka Recorderu ponechá živý stav i atributy dostupné kartě, ale nebude ukládat jejich další změny ani statistiky. Již uložené záznamy tím nesmaže. Ostatní entity zůstávají zaznamenané při změně hodnoty či dostupnosti; neobsahují měnící se atributy stáří.

Po aktualizaci firmware nech GW znovu připojit k MQTT a pak jednou znovu načti integraci MQTT v HA, aby zmizely staré atributy `age_s`/`stale` na běžných entitách. Nahraď také starou kartu tímto YAML; stará verze čte atributy přímo z jednotlivých entit.

„GW online“ používá dostupnost místního nastavení GW **Charge limit enabled**, které nepotřebuje žádná data z CAN. Stavy `on` i `off` znamenají dostupnou gateway; zapnutí samotné funkce není podmínkou. Tato entita má MQTT dostupnost a expiraci stejně jako ostatní. Dostupné údaje vozidla slouží jako další potvrzení spojení. GW proto může být online, i když SOC/BSI a ostatní údaje vozidla zatím nejsou k dispozici. Pokud bylo ID nastavení přejmenováno, oprav položku `gateway` v úvodním bloku karty. Nejde o ping ani potvrzení bdělého auta. Stav BSI se zobrazuje přímo z jeho entity a stáří dat ze SOC. Samotné otevření dashboardu nebudí vozidlo.

Karta sama historická data neukládá. Detail a historii jednotlivých entit lze otevřít z jejich běžného seznamu v HA.
