<p align="center"><img src="assets/logo.png" width="128" alt="DSplay logosu"></p>

<h1 align="center">DSplay</h1>

<p align="center"><b>Nintendo DSi için Switch tarzında bir ana menü ve onu kuran masaüstü uygulaması</b><br>
Sürüm 3.2.0 · Yapan: <b>3g2h1n</b></p>

<p align="center"><a href="https://3g2h1n.github.io/DSplay/">Tanıtım sayfası</a> · <a href="https://github.com/3g2h1n/DSplay/releases/latest">Son sürümü indir</a></p>

---

## DSplay nedir?

DSplay, Nintendo DSi'ın kendi menüsünün yerine geçen yeni bir ana menüdür. Konsol açıldığında
Nintendo Switch'in HOME Menüsü'ne benzeyen bir ekran gelir: oyunların kapaklarıyla yan yana dizilir,
seçim halkası, yumuşak sesler, profiller, klasörler ve ayarlar vardır. Buradan DS oyunları ve eski
konsolların oyunları tek dokunuşla açılır.

Proje iki parçadan oluşur:

| Parça | Ne işe yarar | Nerede çalışır |
|---|---|---|
| **DSplay** | Ana menünün kendisi. SD kartta `BOOT.NDS` olarak durur, Unlaunch onu her açılışta başlatır. | Nintendo DSi / DSi XL |
| **DSplay Studio** | SD kartı hazırlar: kurulum, oyun ve kapak ekleme, profiller, temalar, sesler, açılış ekranı, kayıt yedekleri, kablosuz bağlantı. | macOS, Windows, Linux (Python 3.8+) |

DSplay konsolun dahili belleğine (NAND) hiçbir şey yazmaz; her şey SD karttan çalışır. Bu yüzden
konsolu bozma riski yoktur. Tek ön koşul konsolda daha önce
[Unlaunch](https://dsi.cfw.guide) kurulmuş olmasıdır.

> Bu proje **Ege Şahin** tarafından, Anthropic'in yapay zekâ asistanı **Claude** yardımıyla geliştirildi.
> Fikir, tasarım kararları ve gerçek DSi üzerindeki testler Ege'ye; kodun büyük bölümü, testler ve
> bu belge Claude ile birlikte yazıldı.

---

## Özellikler

### Konsolda (DSplay)

- **Switch tarzı ana ekran:** son oynananlar şeridi, büyük kapak, oyun bilgisi, yuvarlak kısayol
  düğmeleri, alt tuş ipuçları. Açık ve koyu tema, 6 dil (Türkçe, Azerbaycan Türkçesi, İngilizce, İspanyolca,
  Almanca, Fransızca). Azerbaycanca seçilince klavyeye Ə harfi gelir.
- **Tüm yazılımlar:** sistemlere göre sekmeler, sıralama, dokunmatik klavyeyle arama, favoriler
  (ana ekranda L veya R ile doğrudan açılır), gizleme, klasörler (Tüm Oyunlar'da START ile yeni
  klasör oluşturulur), oyun notları.
- **Profiller:** en fazla 4 kişi; her birinin resmi, rengi, 4 tuşluk şifresi, kayıtları, oynama
  süreleri ve ayarları ayrıdır.
- **Takılı oyun kartı:** kartuş ana ekranda görünür ve oradan açılır. Açılışta kart takılı olsa da
  "kart takıldı" sesi çalmaz.
- **Düzenlenebilir ana ekran:** hangi düğmelerin, döşemelerin ve kaç oyunun görüneceğini
  Ayarlar → Ana ekran'dan seçersin.
- **Açılış ekranı:** DSplay logolu animasyon, ya da Studio'da kendi resmin ve sesinle hazırladığın açılış.
- **Oyuna özel ayarlar:** her DS oyunu için işlemci hızı, DS/DSi modu ve VRAM hızlandırma
  (oyunda **Y → Oyun ayarları**).
- **Hileler (Action Replay):** oyunda **Y → Hileler**. Veritabanındaki hileler klasörleriyle listelenir;
  A ile açıp kapatırsın, oyun açılırken nds-bootstrap seçtiklerini yükler. Veritabanı (`usrcheat.dat`)
  TWiLight Menu++ ile ortaktır, seçimler ikisinde de aynı kalır. Studio'dan tek tuşla kurulur.
- **Fotoğraflar:** DSi Kamerası'nın çektikleri ve `sd:/photos` içindeki JPEG/PNG resimler.
  - **Albümler:** L / R ile Tümü, ♥ Favoriler ve her ay için bir albüm. X fotoğrafı favori yapar (profil başına).
  - **Ayarla (Y):** duvar kağıdı, profil resmi, açılış ekranı, oyun kapağı, klasör resmi ya da (beta)
    Yakındakilerle paylaşım. Yön tuşları / kalem ile kırpma alanı taşınır, L / R küçültür / büyütür.
    Oyun kapağı `_hyper/covers/<dosya adı>.png` olarak yazılır ve indirilen kapaktan önce gelir.
  - **Ekran koruyucu:** Ayarlar → Görünüm → Ekran koruyucu (Kapalı / 1 / 3 / 5 / 10 dk, varsayılan 3 dk):
    fotoğraflar üst ekranda yavaşça kayıp yakınlaşarak geçer, alt ekranda saat durur. Bir tuş ya da dokunuş
    sadece geri döndürür. Kaynak: tüm fotoğraflar ya da favoriler.
  - **Şarjda fotoğraf çerçevesi:** konsol şarjdayken ekran koruyucu bir dakika sonra başlar ve ekran kararmaz.
  - **Duvar kağıdını sıfırla:** Ayarlar → Görünüm; profilin duvar kağıdı kaldırılır (`wallpaper.bin.old`
    olarak saklanır), temanın kendi arka planı geri gelir.
  - **Günün fotoğrafı:** 30 saniye boşta kalınca çıkan büyük saatin arkasında her gün başka bir fotoğraf.
  - **Yakındakiler (beta):** paylaştığın fotoğraf profil kartınla birlikte gider (128x96, 24 paket);
    arkadaşının fotoğrafı onun kartının arkasında görünür, A ile `photos/Nearby/` içine kaydedilir.
  - **Studio → Temalar → Fotoğraflar:** bir klasörü ya da seçilen fotoğrafları (tarayıcının açabildiği her
    biçim) 640x480'e küçültüp `sd:/photos/<albüm>/` içine JPEG olarak aktarır.
- **Açılış:** konsol açılınca açılış animasyonu ve kullanıcı seçimi gelir; bir oyundan çıkınca (DSplay'in
  başlattığı oyun ya da kart) animasyon ve seçim olmadan doğrudan o kullanıcının ana ekranı açılır. Bunu
  DSplay'in ARM7'si MCU'nun boş bir yazmacına (0x71) oyunu başlatırken not ederek anlar; bu yazmaç konsol
  kapanınca sıfırlanır. Yedek sürümde (hazır ARM7) eski davranış sürer.
- **Menü sesleri:** konsolda tek paket var (DSplay). Klasik DSi tarzı sesler Studio → Sesler → **Klasik DSi
  seslerini indir** ile `_hyper/sfx/` içine yazılır.
- **Boşta saat:** ana ekranda 30 saniye dokunulmazsa üst ekranda büyük bir saat çıkar.
- **Oynama rozetleri:** 1, 10, 50 ve 100 saatte oyunun kartında madalya belirir.
- **Kumanda (PC bağlantısı, Wi-Fi):** DSi'yi bilgisayar için kablosuz oyun kolu, fare, sunum
  kumandası, medya kumandası ya da makro paneli olarak kullanma; mikrofona üfleyerek tuşa basma;
  üst ekranda bilgisayarın CPU, RAM ve pil durumu ile çalan şarkı. Varsayılan olarak kapalıdır;
  Ayarlar → Ana ekran'dan eklenir.
- **Uygulamalar:** Flipnote Studio, DSi Kamera, DSi Ses, Sistem Ayarları ve DSi'ına kurduğun diğer
  DSiWare'ler Tüm Oyunlar'ın ilk sekmesinde durur. Uygulamalar sekmesinde **START → Uygulama ekle**
  ile DSi'ın dahili belleğinde kurulu olanları listeler, istediklerini işaretlersin. Dahili belleğe
  hiçbir şey yazılmaz; uygulama Unlaunch üzerinden açılır.
- **DSiWare oyunları (.nds):** SD karttaki DSiWare dosyaları (ör. internetten indirilmiş DSiWare'ler)
  normal oyunlar gibi listelenir ve Unlaunch ile konsolun kendi yoluyla açılır (TWiLight Menu++'taki
  "DSiWare başlatıcı: Unlaunch" ile aynı). Kayıtları oyunun yanında `oyun.pub` / `oyun.prv` olarak
  durur; yoksa DSi'ın biçiminde oluşturulur, varsa asla üzerine yazılmaz. Oyundan çıkmak için POWER'a
  kısa bas: konsol yeniden başlar, DSplay açılır.
  - **Profil başına kayıt:** ilk profilin kaydı oyunun yanında kalır; diğer profillerinki
    `_hyper/profiles/pN/saves/` içinde. Başlatırken profilin kaydı oyunun yanına kopyalanır (oradaki
    kayıt `_hyper/dsw/hold.*` olarak kenara alınır); DSplay bir sonraki açılışta oynanan kaydı profile
    geri koyar, kenara alınanı yerine döndürür. Adımlar `_hyper/dsw/pending.txt` ile izlenir, her kopya
    karşılaştırılmadan hiçbir dosya silinmez; yarıda kalırsa bir sonraki açılışta tamamlanır.
  - **Başlatıcı seçimi:** Seçenekler → Oyun ayarları → Başlatıcı: **Unlaunch** (varsayılan) ya da
    **nds-bootstrap** (deneysel). nds-bootstrap kısa 8.3 yollar ister: kayıt `_hyper/dsw/SAVE.PUB/PRV`
    olarak, yolu uzun olan oyun `_hyper/dsw/APP.NDS` olarak kopyalanıp verilir.
- **Ana ekran düğmeleri:** Ayarlar → Görünüm → Ana ekranı düzenle: alttaki yuvarlak düğmelere **Kumanda**,
  **Yakındakiler**, **Fotoğraflar** ve **Güç** eklenebilir (Güç varsayılan olarak gizli, çünkü POWER tuşu aynı
  menüyü açar; yedek sürümde her zaman görünür). Aynı yerden **Oyun kartı: kapak altta da** açılırsa takılı
  kartın kutu resmi alttaki kutucukta da gösterilir.
- **Oyun adları:** banner'daki ad yazı tipinde olmayan harflerden oluşuyorsa (Japonca, Çince) başka bir dildeki
  ad, `_hyper/titles.txt`'teki ad, dosya adı ya da kartuş başlığı kullanılır; "????" görünmez.
- **Güç menüsü:** konsolun POWER tuşuna kısa basınca ya da ana ekrandaki güç düğmesine dokununca açılır
  (tekrar basınca kapanır; basılı tutmak konsolu her zamanki gibi hemen kapatır): **Kapat**,
  **Yeniden başlat**, **DSi Menüsü**. Kapatmadan önce SD kart düzgünce kapatılır. Bunun için DSplay
  kendi ARM7 programını kullanır (`arm7/`). `assets/payloads/DSplay_yedek.nds` aynı menünün konsolun hazır
  ARM7 programıyla derlenmiş yedeğidir (orada POWER tuşu DSi Menüsü'ne gider, "Kapat" görünmez).
- **Dokunmatik:** Ayarlar → **Dokunmatik ayarı**: kalemle 5 hedefe dokunursun, DSplay kayma, ölçek ve
  hafif eğikliği birlikte düzeltir (sadece DSplay'de geçerlidir, oyunlar sistem ayarını kullanır; Y ile
  sisteme dönülür). Dokunduğun yer hemen işaretlenir, basılan döşeme ve düğme içe çöker, dokunuş
  alındığında bir halka yayılır. A tuşuna basınca da seçili döşeme/düğme aynı şekilde içe çöker. Menü meşgulken yapılan kısa dokunuşlar ve tuş basışları kaybolmaz.
- **Beta özellikleri:** Mağaza ve Yakındakiler şimdilik denemede; Ayarlar → Sistem → **Beta özelliklerini dene**
  açılınca ana ekranda görünürler (varsayılan: kapalı).
- **Wi-Fi:** Ayarlar → Sistem → **Wi-Fi**: yakındaki ağlar listelenir (sinyal gücü, kilit), seçilen ağın şifresi
  ekran klavyesiyle yazılır (↑ büyük harf, #+= semboller) ve hemen denenir. Bağlanırsa `_hyper/wifi.txt`'e
  kaydedilir; bağlanamazsa önceki kayıtlı ağ geri gelir. Kumanda, Mağaza ya da Yakındakiler Wi-Fi'ye
  bağlanamazsa bir uyarı çıkar: **Tekrar dene** ya da **Wi-Fi ayarları**.
- **Ses / parlaklık göstergesi:** konsolun ses tuşlarıyla (ya da SELECT + ses ile parlaklık) oynayınca üst
  ekranda, ad ile saat arasında küçük bir çubuk belirir ve bir süre sonra kaybolur (DSplay'in kendi ARM7'si
  gerekir; yedek sürümde yoktur).
- **Mağaza (beta, deneysel):** ana ekranın alt düğmelerinde. DS ve DSi için ücretsiz homebrew oyunlar, araçlar ve emülatörler (Universal-DB kataloğu,
  3DS'teki Universal-Updater'ın kullandığı veritabanı). Konsoldaki Mağaza kutucuğu eShop gibi
  çalışır: kategoriler, simgeler, açıklamalar, "İndir" düğmesi. İndirmeyi bilgisayardaki Studio yapar
  ve dosyayı Wi-Fi ile konsola gönderir, bu yüzden Studio açık ve konsol eşleşmiş olmalı.
- **Hareketli simgeler (beta):** DSi'ya özel oyunların hareketli simgesi üst ekranda oynar.
  Ayarlar → Hareketli simgeler ile kapatılabilir.
- **Yakındakiler (StreetPass benzeri):** Aynı Wi-Fi ağında DSplay kullanan iki konsol, bu ekran
  açıkken profil kartlarını (ad, resim, selam mesajı, favori oyun, oynama süresi) birbirine gönderir.
  "Karşılaştıklarım" listesi her profil için ayrı tutulur; selam mesajını X ile yazarsın.
- **Eski menüme dön:** Ayarlar'dan tek dokunuşla kartın DSplay'den önceki menüsüne (ör. TWiLight Menu++) dönülür.
  Satır yalnızca Studio kurulumda DSplay olmayan bir menü bulup yedeklediyse görünür.

### Bilgisayarda (DSplay Studio)

| Sayfa | Yaptığı |
|---|---|
| **SD kart** | Kartı seçer, durumunu gösterir, gerekirse FAT32 olarak biçimlendirir (yalnızca çıkarılabilir kartlar, sürücü adını yazarak onay). |
| **Kurulum** | DSplay'i ve oyunları çalıştıran bileşenleri kurar ya da günceller; GBA BIOS'unu ekler. Kurmadan önce yedek alır. |
| **Oyunlar** | Sürükle bırak ile oyun ekler; `.zip` / `.7z` arşivlerini açar, yanındaki kayıt dosyasını da taşır. Hile veritabanını (usrcheat.dat) internetten indirir ya da dosyadan ekler. |
| **Kapaklar** | Eksik kapakları internetten indirir (GameTDB, libretro-thumbnails). |
| **Profiller** | Profil adı, resmi, rengi, duvar kâğıdı; şifre sıfırlama. |
| **Temalar** | Renk temaları ve kendi açılış ekranın (üst/alt resim, en fazla 5 saniyelik ses). |
| **Sesler** | Menü seslerini kendi WAV dosyalarınla değiştirir. |
| **Kayıtlar** | Bütün kayıtları tek ZIP'e yedekler, geri yükler; kart anlık görüntüleri; DSplay'i kaldırıp kartı eski haline getirme. |
| **Kumanda** | DSi ile eşleşme, tuş düzenleri, makrolar, fare ve mikrofon ayarları. |
| **Kablosuz** | Kartı çıkarmadan DSi'ya oyun ve kapak gönderme, kayıtları Wi-Fi üzerinden yedekleme. |
| **Mağaza** | Homebrew kataloğu: arama, kategoriler, "Karta kur", "Konsola gönder" (Wi-Fi) ve kaldırma. |

### Çalıştırılabilen sistemler

| Sistem | Uzantılar | Çalıştıran |
|---|---|---|
| Nintendo DS / DSi | .nds .dsi .srl .ids | nds-bootstrap |
| DS homebrew | .nds | doğrudan ya da nds-bootstrap-hb |
| Game Boy Advance | .gba .agb | GBARunner2 (`bios.bin` gerekir) |
| Game Boy / Color | .gb .sgb .gbc | GameYob |
| NES / Famicom | .nes .fds | nesDS |
| Super Nintendo | .sfc .smc | SNEmulDS |
| Mega Drive / Genesis | .md .gen .bin | PicoDriveTWL / jEnesisDS |
| Master System / Game Gear | .sms .gg | S8DS |

---

## Kurulum

### 1. Studio'yu aç

Bilgisayarda Python 3.8 veya daha yenisi kurulu olmalı ([python.org](https://www.python.org/downloads/)).
Başka bir şey gerekmez.

- **macOS:** `DSplay Studio.command` dosyasına çift tıkla.
  (İlk seferde "geliştirici doğrulanamadı" derse: sağ tık → Aç.)
- **Windows:** `DSplay Studio (Windows).bat` dosyasına çift tıkla.
  Python kurarken "Add python.exe to PATH" kutusunu işaretle.
- **Linux:** `./dsplay_studio.sh`

Studio kendi penceresinde açılır. Yalnızca bu bilgisayardan erişilebilir; ağdaki başka biri bağlanamaz.
Studio'nun dili sol alttan seçilir (Türkçe, Azərbaycanca, English, Español, Deutsch, Français, Português, Русский).

### 2. Kartı hazırla

1. **SD kart** sayfasında kartını seç. Kart FAT32 değilse buradan biçimlendir.
   (Windows 32 GB'tan büyük kartları FAT32 yapamaz; bunun için "guiformat" aracını kullan.)
2. **Kurulum** sayfasında profil adını ve dili seçip **Kur / Güncelle**'ye bas.
3. GBA oyunları için kendi GBA'ndan aldığın 16 KB'lık `bios.bin` dosyasını aynı sayfadan ekle.
4. **Oyunlar** sayfasına oyunlarını sürükle, **Karta kopyala**'ya bas.
5. İstersen **Kapaklar**'dan kapakları indir.
6. **Güvenle çıkar** ile kartı çıkar.

### 3. DSi'ı DSplay ile açılacak şekilde ayarla (Unlaunch)

1. Kartı tak, **A + B** basılıyken konsolu aç.
2. **OPTIONS → NO BUTTON →** "DSplay" (`sd:/BOOT.NDS`) seç.
3. **SAVE & EXIT**.

Artık konsol her açıldığında DSplay gelir.

---

## Konsolda tuşlar

| Tuş | Ana ekran | Tüm yazılımlar |
|---|---|---|
| A | başlat | başlat |
| B | geri | ana ekrana dön |
| X | favori | favori |
| Y | seçenekler (kayıt slotu, oyun ayarları, hileler, klasöre ekle, not, kayıt geri yükleme, gizle, sil) | seçenekler |
| L / R | favoriler | sekmeler |
| SELECT | arama | arama |
| START | ayarlar | klasörler (yeni klasör, adını değiştir, oyun seç); Uygulamalar sekmesinde: uygulama ekle |

Dokunmatik ekran her yerde çalışır.

---

## Kayıtların güvende: denemek isteyenler için

DSplay'i kurmak hiçbir şeyini kaybettirmez; beğenmezsen kartını eski haline getirmek tek tuş.

**Kurulumdan önce**
- Studio karttaki **bütün kayıtları** ve kurulumun değiştireceği **her dosyayı** bilgisayara yedekler,
  sonra yedeği okuyup kontrol eder. Yedek alınamazsa kurulum hiç başlamaz.
- İlk kurulumda istersen kartın tamamının kopyası da alınır.
- Yedekler: `Belgeler/DSplay Backups/Kart yedekleri/`
  (Eski sürümün `HyperLauncher Backups` klasörü varsa Studio onu otomatik olarak bu ada çevirir.)

**Beğenmezsen iki yol var**
- **Konsoldan:** Ayarlar → **Eski menüme dön**. Bir sonraki açılışta kartın eski menüsü gelir.
- **Studio'dan (tam geri dönüş):** Kayıtlar → **DSplay'i kaldır, kartı eski haline getir**.
  Eski menü ve onun dosyaları geri gelir, kurulumun eklediği dosyalar silinir.

**Kayıtlar neden kaybolmaz**
- Hiçbir işlem bir kaydı silmez ya da eski bir sürümle değiştirmez. Geri dönüşte karttaki kayıt yerinde
  kalır; yedekte farklı bir sürümü varsa o da `_hyper/save_conflicts/` içine konur.
- Kartı değiştiren her işlemden önce (kurulum, geri dönüş, biçimlendirme, kayıt geri yükleme) kayıtlar
  bir kez daha yedeklenir.
- Konsolda bir oyunu silersen kaydı silinmez, `_hyper/deleted_saves/` klasörüne taşınır.
- Her oyun açılışında kaydın yedeği alınır; son 3 farklı sürüm saklanır (`_hyper/backups/`).
- Kayıtlar önce geçici bir dosyaya yazılır, kontrol edilir, sonra yerine konur. Pil bitse bile yarım
  bir kayıt kalmaz.
- Kablosuz aktarım var olan bir kaydın üzerine asla yazmaz.

---

## Kumanda ve kablosuz aktarım

1. DSi'da Ayarlar → Ana ekran → **Kumanda**'yı aç; ana ekranda Kumanda döşemesi belirir.
2. DSi ile bilgisayar **aynı Wi-Fi ağında** olmalı. DSi'ın kendi Wi-Fi ayarı yoksa ağ adını ve
   şifresini Studio'nun Kumanda sayfasından karta yazabilirsin.
3. DSi'da Kumanda'yı aç, ekrandaki 4 haneli kodu Studio'daki Kumanda sayfasına gir.
   Bulamazsa DSi ekranında yazan IP adresini Studio'ya elle yaz.
4. Bağlanınca Studio'nun **Kablosuz** sayfası açılır: oyun ve kapak gönderme, kayıt yedekleme.

Konsoldaki **Mağaza** da bu bağlantıyı kullanır: Mağaza'yı açınca konsol Studio'ya bağlanır,
katalog gelir ve "İndir" dediğin program Studio tarafından indirilip karttaki `roms/NDS/Homebrew`
klasörüne gönderilir. **Yakındakiler** ise yalnızca Wi-Fi ister, bilgisayar gerekmez.

Ağ bağlantıları: UDP 39510 (DSi), 39511 (bilgisayar), TCP 39512 (dosya aktarımı), UDP 39513
(Yakındakiler). Bilgisayarın
güvenlik duvarı bunlara izin vermeli. DSi 2.4 GHz açık, WEP ve WPA/WPA2 ağlarına bağlanır
(5 GHz ve WPA3 olmaz). Misafir ağlarında ve "istemci yalıtımı" açık modemlerde cihazlar birbirini
göremez; bu durumda bağlantı kurulamaz.

---

## Klasörler

### Proje

```
DSplay/
├── DSplay Studio.command          macOS: Studio'yu açar (çift tıkla)
├── DSplay Studio (Windows).bat    Windows: Studio'yu açar (çift tıkla)
├── dsplay_studio.sh               Linux: Studio'yu açar
├── launcher.py                    Studio'nun başlangıç dosyası
├── Makefile                       derleme ve testler
├── source/, include/              DSplay'in kaynak kodu (C, ARM9)
├── arm7/                          DSplay'in ARM7 programı (calico'nunki + "kapat" isteği)
├── studio/                        DSplay Studio (Python, ek kurulum gerekmez)
│   └── web/                       Studio'nun arayüzü (HTML/CSS/JS)
├── assets/
│   ├── logo.png, icon.bmp         logo ve DSi menü simgesi
│   └── payloads/                  karta kurulan dosyalar
│       ├── BOOT.NDS               derlenmiş DSplay
│       ├── DSplay_yedek.nds       aynı DSplay, konsolun hazır ARM7 programıyla (yedek)
│       ├── HyperLauncher.nds      aynı dosya (eski kartlarla uyumluluk için bu adla)
│       └── HyperDSi-Runtime.zip   nds-bootstrap, GBARunner2 ve emülatörler
├── tests/                         otomatik testler
├── tools/                         yazı tipi, simge, logo üreticileri; komut satırı araçları
│   ├── prepare_sd.py              Studio'nun komut satırı sürümü
│   └── build_standalone.py        Studio'yu Python gerektirmeyen .app / .exe yapar
└── third_party/                   dswifi (Wi-Fi), stb_image (PNG), TJpgDec (JPEG)
```

### SD kart

```
BOOT.NDS, HyperLauncher.nds   DSplay
_nds/                         nds-bootstrap, GBARunner2, TWiLight Menu++ emülatörleri
_nds/TWiLightMenu/extras/usrcheat.dat   hile veritabanı (isteğe bağlı)
_gba/bios.bin                 GBA BIOS (sen eklersin)
_hyper/apps.bin               eklenen DSi uygulamaları
_hyper/store/                 mağaza kataloğu ve kurulanların listesi
roms/NDS/Homebrew/            mağazadan kurulan programlar
_hyper/                       DSplay'in verileri: ayarlar, profiller, kütüphane, kapaklar,
                              sesler, açılış ekranı, yedekler
roms/NDS, GBA, GameBoy, NES, SNES, Genesis, MasterSystem, GameGear
photos/                       fotoğraf görüntüleyicinin okuduğu resimler (DCIM de okunur)
```

Klasör adları önemli değildir: DSplay `roms/` ve `games/` klasörlerini (4 seviyeye kadar) ve kartın
kökünü tarar, sistemi dosya uzantısından anlar.

Bazı iç adlar (`HyperLauncher.nds`, `_hyper/`) projenin eski adı olan HyperLauncher DS'ten kalmadır;
daha önce kurulmuş kartlarla uyumlu kalmak için değiştirilmedi.

---

## Geliştirenler için

```
# devkitPro: https://devkitpro.org/wiki/Getting_Started
dkp-pacman -Syu && dkp-pacman -S nds-dev

make               # DSplay'i derler -> assets/payloads/BOOT.NDS
make test          # konsol kodunun testleri (bilgisayarda çalışır)
make test-studio   # Studio testleri
make test-pad      # kumanda / kablosuz aktarım testleri
make sim           # arayüzün ekran görüntülerini çıkaran simülatör
make studio        # Studio'yu başlatır

python3 tools/prepare_sd.py --sd /Volumes/DSI --install --lang tr --profile Ege
python3 tools/build_standalone.py      # Python'suz Studio (PyInstaller gerekir)
```

devkitPro'yu güncel tut: eski devkitARM ile yeni libfat birlikte kullanılırsa `fatInitDefault()` içinde çöker.

Simge ve logolar koddan üretilir: `tools/gen_icons.py`, `tools/gen_logo.py`, `tools/gen_font.py`.

---

## Bilinen sınırlar

- Oyunlar ve GBA BIOS telif nedeniyle pakette yoktur; yalnızca sahip olduğun oyunların yedeklerini kullan.
- Oyunu askıya alıp menüye dönmek (Switch'teki gibi) DSi'da mümkün değildir; donanım aynı anda tek program tutar.
- DSi kamerası bilgisayara webcam olarak bağlanamaz.
- Hile veritabanı internetten indirilemezse (adres değişmiş olabilir) `usrcheat.dat` dosyasını
  [GBAtemp'teki DeadSkullzJr başlığından](https://gbatemp.net/threads/deadskullzjrs-nds-i-cheat-databases.488711/)
  indirip Studio'da "usrcheat.dat seç" ile ekleyebilirsin. Hileler bazı oyunlarda kayıtları bozabilir.
- SNES ve bazı Mega Drive oyunlarında hız ve ses sınırlıdır; bu emülatörlerin sınırıdır.
- Kapak indirme GameTDB ve libretro-thumbnails sunucularına bağlıdır.
- Mağaza bilgisayardaki Studio olmadan çalışmaz: DSi günümüzün şifreli internet bağlantılarını kuramaz.
  Kataloğu Universal-DB sağlar; dahili belleği ya da flash kartları değiştiren araçlar (hiyaCFW, NAND
  yöneticileri, kart yazıcılar) bilerek listeden çıkarıldı. Bazı programlar `.nds` dosyasından başka
  veri de ister; onları proje sayfasındaki talimatlarla kurmak gerekir.
- Yakındakiler'de iki konsolda da ekran aynı anda açık olmalı. 3DS'teki gibi konsol kapalıyken çalışmaz.
  Misafir ağları ve "istemci yalıtımı" açık modemler konsolların birbirini görmesini engeller.
- Uygulamalar dahili bellekten okunur; DS modunda (flash kartla) bu sekme görünmez.
- Unlaunch kuruluyken SD kart takılıysa DSi Menüsü'nden ya da Sistem Ayarları'ndan kapatmak 5-10 saniye
  sürer (Unlaunch'ın bilinen bir yan etkisi, DSplay'den değil). Konsolu DSplay açıkken POWER'a basılı
  tutarak kapat; hemen kapanır.

## Sorun olursa

Studio'nun hata mesajını ya da proje klasöründe oluşan `studio_hata.txt` dosyasını sakla.
Yedeklerine dokunma; hepsi `Belgeler/DSplay Backups/` içinde.

---

## Teşekkürler ve lisanslar

DSplay şu açık kaynak projelerin üzerine kurulu. Her biri kendi lisansıyla dağıtılır:

- [devkitPro](https://devkitpro.org), libnds, calico, libfat: derleme araçları ve DS kütüphaneleri
- [dswifi](https://github.com/devkitPro/dswifi): Wi-Fi (MIT, `third_party/dswifi/dswifi_license.txt`)
- [stb_image](https://github.com/nothings/stb): PNG çözme (MIT / kamu malı)
- [TJpgDec](http://elm-chan.org/fsw/tjpgd/): JPEG çözme (ChaN, serbest lisans)
- [TWiLight Menu++](https://github.com/DS-Homebrew/TWiLightMenu), [nds-bootstrap](https://github.com/DS-Homebrew/nds-bootstrap),
  [GBARunner2](https://github.com/Gericom/GBARunner2) ve emülatörleri (GPL): `HyperDSi-Runtime.zip` içinde
- [Universal-DB](https://db.universal-team.net) (Universal-Team): mağaza kataloğu
- [Inter](https://rsms.me/inter/) ve Noto yazı tipleri (SIL Open Font License)

Arayüz Nintendo Switch'ten esinlenmiştir ama Nintendo'ya ait hiçbir resim, ses, yazı tipi ya da logo
içermez: sesler kodla üretilir, simgeler ve logo bu proje için çizilmiştir. Nintendo DSi, Nintendo
Switch ve ilgili adlar Nintendo'nun ticari markalarıdır; bu proje Nintendo ile bağlantılı değildir.

---

<p align="center">Ege Şahin · Claude (Anthropic) yardımıyla · 2026</p>

## Tanıtım sayfası ve sürümler (GitHub)

- **Tanıtım sayfası** `docs/` klasöründedir ve GitHub Pages ile yayınlanır. Bir kez
  **Settings → Pages → Build and deployment → Source: Deploy from a branch**, **Branch: `main`**,
  klasör **`/docs`** seçilip kaydedilir. Sonrasında her push sayfayı kendiliğinden günceller.
  Sayfa repo adresini kendisi bulur (`kullanıcı.github.io/repo` → `github.com/kullanıcı/repo`).
- **Sürüm yayınlamak:** bir sürüm etiketi göndermek yeterli:
  `git tag v3.2.0 && git push origin v3.2.0`. `.github/workflows/release.yml` repodan
  `DSplay-3.2.0.zip` dosyasını hazırlar ve bir GitHub Release olarak yayınlar. Tanıtım
  sayfasındaki "GitHub'dan indir" düğmesi doğrudan bu zip'e bağlanır.

