# Home Assistant → GW: příkazy vozidlu

První implementovaná operace je **`vehicle.refresh`**, stejná sekvence jako
Refresh v telefonu: bounded wake/394 a ověření novou platnou SOC odpovědí 3E0.
Refresh sám nenastavuje režim nabíjení ani neovládá AC charger. Úspěch znamená
obnovení dat; skutečné zahájení nabíjení musí řízení ověřit zvlášť podle stavu
vozidla nebo výkonu/proudu chargeru.

## Tlačítko v HA

Po OTA a MQTT discovery přibude u zařízení **Refresh vehicle**, obvykle
`button.ecmpilot_gateway_refresh_vehicle`. Lze jej stisknout v HA nebo volat:

```yaml
action: button.press
target:
  entity_id: button.ecmpilot_gateway_refresh_vehicle
```

Hotový obsah jednoho skriptu je v [opel-refresh-script.yaml](opel-refresh-script.yaml).
Přejmenovanou entitu oprav podle skutečného ID. Tlačítko nevyžaduje dostupný SOC:
právě refresh jej může získat. Vypnutý MQTT, testovací režim, pozastavený CAN nebo
nepodporovaný profil neposkytnou fyzický refresh. Stisk tlačítka sám nepotvrzuje
dokončení ani nabíjení. Tlačítko připraví nové ID a aktuální boot automaticky.
ID kombinuje čas v mikrosekundách se dvěma náhodnými 16bitovými částmi;
každý rozsah `range(0, 65536)` se vejde do limitu Jinja sandboxu v HA.

## Obálka a topics pro Node-RED / mqtt.publish

Prefix zůstává sestavovací `ecmpilot`. U instalace se sériovým číslem
`288485b41554` je `<node>` **`ecmpilot_gw_288485b41554`**. U jiného zařízení
použij jeho MAC/node.

| Topic | Obsah | QoS / retain |
| --- | --- | --- |
| `ecmpilot/<node>/command/info` | `v`, aktuální `boot`, seznam operací a `request_topic` | 1 / ano |
| `ecmpilot/<node>/command/request` | Požadavek HA/Node-RED | 1 / **ne** |
| `ecmpilot/<node>/command/response` | Přijetí, odmítnutí, výsledek nebo odpověď na dotaz | 1 / ne |

Načti `boot` z info topicu; po novém bootu GW se změní. Nové příkazy musí
použít aktuální hodnotu. Info topic sám nic neprovádí. Účet GW na brokeru musí
mít právo číst request topic a zapisovat své stavové, discovery a response topics.

Požadavek (boot níže je pouze příklad, nahraď skutečným):

```json
{"v":1,"id":"00000000000000000000000000000001","boot":"0123456789abcdef0123456789abcdef","op":"vehicle.refresh","args":{}}
```

ID je 32 malých hexadecimálních znaků. **Nová zamýšlená akce = nové ID.**
Transportní opakování stejné akce musí zachovat ID i obsah. GW uchovává výsledek
refresh příkazu v RAM 10 minut od dokončení a duplicitní doručení v této době
neopakuje fyzickou práci. Také odmítnutí Busy se uchová: další zamýšlený pokus
za 5 minut musí mít nové ID. Cache sdílí 256 míst s telefonem, plná cache odmítá
nové akce. Reboot cache zruší; starý `boot` potom příkaz neautorizuje.

Maximální request má 512 bajtů. Cizí/duplicitní JSON klíče, nesprávné typy,
vnořené neplatné zprávy a nepodporované operace se odmítají nebo zahazují.
Zprávy dodané brokerem s příznakem retained se zahazují bez vykonání. Používej
clean session a retain=false; runtime GW používá čistou MQTT session. Zprávy
čekající lokálně ve frontě déle než 5 sekund nebo přes reconnect se neprovedou.

## Výsledek a dotaz

Odpověď má `v:1`, `type:status`, `boot`, `reply_to`, `op` a `command`:

```json
{"v":1,"type":"status","boot":"0123456789abcdef0123456789abcdef","reply_to":"00000000000000000000000000000001","op":"vehicle.refresh","command":{"id":"00000000000000000000000000000001","revision":1,"domain_rev":1,"phase":"accepted","error":null}}
```

`accepted` je přijetí do řízení. Další zpráva se stejným `reply_to` přinese:

- `succeeded`: refresh potvrzen novou platnou CAN odpovědí.
- `failed`: explicitní chyba existujícího controlleru.
- `unknown`: výsledek nepotvrzen, např. `bsi_wake_timeout`,
  `vehicle_reply_timeout`, `can_tx_failed` nebo `unconfirmed`.
- `rejected`: požadavek nebyl přijat, např. `busy`, `boot_changed`,
  `unsupported_operation`, `invalid_argument`.

QoS1 může doručit odpověď vícekrát. Výsledek zpracuj podle
`command.id` a `command.revision`. `succeeded` **není potvrzení nabíjení**.
Controller omezuje refresh/charge operaci na 25 s, sdílená obálka na 35 s.
Volající má počkat přibližně 40 s; při výpadku transportu lze dotázat cache:

```json
{"v":1,"id":"00000000000000000000000000000002","boot":"0123456789abcdef0123456789abcdef","op":"command.get","args":{"command_id":"00000000000000000000000000000001"}}
```

`reply_to` je ID dotazu, `command.id` původní akce. Dotaz nikdy nebudí auto.
`result_unknown` znamená chybějící/ztracený výsledek, nikoli prokázané neprovedení
akce. MQTT dotazy nemohou přebírat účtenky telefonního klienta.

## Vlastnictví a další operace

```text
MQTT callback → bounded request queue → UDP command owner/cache
                                      → CPU1 vehicle controller → TaskCan
MQTT publisher ← bounded response queue ← admission / terminal receipt
```

MQTT a telefon se vzájemně nepřepíšou: souběžná explicitní akce vrací Busy.
Automatický refresh/charge limiter si zachovají současnou prioritu a pravidla.
Discovery, reconnect, telemetrie a čtení výsledků nevyvolávají refresh.

Verze obálky, explicitní `op` a samostatné `args` umožňují další operace přidat
po jedné. Následující operace zatím přes MQTT **nejsou povolené**:

| Budoucí op | Navazující existující řízení |
| --- | --- |
| `climate.set` | Předehřev Start/Stop, ověření odezvou |
| `charge.mode.set` | Immediate/deferred a lokální čas, včetně konfliktů revizí |
| `climate.programs.set` | Změna nativních týdenních programů s compare-and-set |

Při rozšíření se doplní validátor argumentů, pravidla konfliktů/revizí a testy;
nebude se přidávat druhý CAN vysílač. AC charger zůstává samostatně řízený z HA.

## Ověření

`tests/test_ha_commands.py` vykonává skutečný MQTT adaptér, společnou cache,
controller a simulované CAN readbacky. `tests/test_lcd_telemetry.py` kontroluje
discovery a oddělení tlačítka od telemetrie. Sestavení ESP ověřuje kompilaci
front a fragmentovaného MQTT příjmu proti skutečnému ESP-IDF API. Živý MQTT broker,
stisk tlačítka v HA a vozidlo musí následně ověřit uživatel; offline testy
neprokazují, že refresh fyzicky zahájí nabíjení.
