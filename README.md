# Tüketici Launcher C++

Bu proje, `script8.py` içindeki Minecraft launcher işlevlerini Qt 6 ve C++ ile yeniden kurar. Arayüz, gönderdiğin ekran görüntüsündeki koyu tema, sol tarafta oyun alanı ve sağ tarafta hesap/işlem panellerini izler. Uygulama önce küçük bir açılış penceresi gösterir, kısa bir hazırlık süresinden sonra ana pencereyi tam ekrana açar.

## Özellikler

- Minecraft sürüm manifesti, vanilla kurulum ve Fabric, Forge, NeoForge, Quilt profili kurulumu
- Minecraft sürümünün gerektirdiği Java yoksa resmi Eclipse Temurin/Adoptium API'sinden HTTPS üzerinden indirme ve SHA-256 doğrulaması
- Birden fazla çevrimdışı ve Microsoft hesabı arasında geçiş; Microsoft tokenları Windows'ta DPAPI ile saklama
- Çevrimdışı profil, sürüm ve oyun profili seçimi; profil oluşturma, kopyalama, yeniden adlandırma, silme, içe ve ZIP olarak dışa aktarma
- Modrinth ve CurseForge arama/indirme; çevrimiçi sayfa açıldığında popüler Modrinth sonuçlarını gösterme ve indirme sayısı/ada göre sıralama; mod kartlarında görsel önizleme, proje galerisi ve bulunan video bağlantılarını açma; Modrinth ve CurseForge modpack kurulumu
- Mod, resource pack, shader, data pack ve dünya indirme; kurulu modları etkinleştirme, devre dışı bırakma ve silme
- Skin indirme, yerel PNG uygulama, Microsoft hesabına skin yükleme; seçili çevrimdışı PNG skinini Horizon 26.3'e aktarma
- Sunucu durum sorgusu, Minecraft ekran görüntüsü galerisi, performans ayarları ve launcher/oyun logları
- Horizon Minecraft Java 26.3 Rust istemcisini resmî GitHub Nightly arşivinden doğrudan indirme, SHA-256 karşılaştırması, kendi Minecraft kurulumundan kaynak hazırlama ve istemciyi başlatma (Horizon Launcher kurulmaz)
- Titan içinden Horizon 26.3 sunucusu oluşturma, başlatma/durdurma, durum ve yerel web yönetim paneli; EULA onayı gerekir, sunucu varsayılan olarak yalnızca bu bilgisayara bağlanır
- Horizon Rust mod kataloğu: varsa resmî katalog ikonlarını gösterme; `.hmod` içerik paketlerini ve `.hnmod` yerel Rust kodu paketlerini yükleme; yayımlayıcı Minisign imzasını kontrol etme ve onaydan sonra ayrı Rust istemcisi derleme
- RAM ve Java seçimi; Windows'un gördüğü, o anda kullanılabilir ve firmware'in bildirdiği kurulu RAM'i ayrı gösterme; GC seçenekleri, yüksek öncelik ve GPU tercihi

Launcher'ın web istekleri ve dosya indirmeleri HTTPS kullanır; HTTPS dışı bağlantılar ve HTTPS'ten HTTP'ye yönlendirmeler reddedilir. Temurin arşivi resmi Adoptium API metaverisinden seçilir ve SHA-256 değeri eşleşmeden açılmaz. CurseForge API ile arama ve indirme için Ayarlar sayfasına kendi API anahtarını eklemek gerekir.

Horizon desteği, `horizon-rust/horizon` GitHub Nightly arşivini ve aynı GitHub sürümündeki `SHA256SUMS` değerini kullanır; detached signature doğrulaması yapmaz. İlk hazırlık seçilen yerel Minecraft Java 26.3 dosyalarını, Mojang sunucu arşivini ve Java 25 JDK'yı kullanır. Horizon Launcher kurulmaz ve Titan hesap sahipliğini denetlemez. Minecraft oyun dosyaları launcher ile dağıtılmaz. Horizon sunucusu yalnızca Java Edition 26.3 içindir; vanilla 26.3 istemcileri bağlanabilir, ancak Spigot/Bukkit eklentileri ve farklı oyun sürümleri desteklenmez. Titan'ın yerel Rust istemcisi hesap doğrulaması yapamadığı için oluşturulan sunucu çevrimdışı kimlik doğrulama kullanır; whitelist açıktır ve varsayılan ağ bağlaması yalnızca bu bilgisayardır. LAN seçeneği açılırsa oyuncu adları doğrulanmaz, bu nedenle sunucuyu internete port yönlendirmeyle açma.

Horizon mod kataloğunda Titan, mod dosyasının SHA-256 değerini resmî dizinle karşılaştırır. Dizinde yayımlayıcı anahtarı olan projelerde her paket Minisign ile bu anahtara karşı doğrulanır; anahtarsız projeler imzasız olarak gösterilir ve ayrı uyarıyla onay ister. İmza, dosyanın belirtilen anahtarla yayımlandığını doğrular; kodun güvenli olduğunu göstermez. Minisign 0.12 Windows x64 arşivinin SHA-256 değeri kaynakta sabitlenmiştir; doğrulama aracı Titan'ın kendi veri klasörüne indirilir.

`.HNMOD` sandbox içinde çalışmaz: Rust kodu ve bağımlılıkların derleme betikleri derleme sırasında, mod da oyun sırasında kullanıcı hesabının izinleriyle çalışır. Her derlemeden önce mod kimlikleri ve bu risk gösterilir. İlk derlemede Titan, Rust 1.99.1 GNU araç zincirini, MinGW-w64 GCC 16.2.0'ı, ilgili `horizon-mixins` commit'ini ve Horizon kaynağını kendi veri klasörüne indirir; araç zinciri ve MinGW SHA-256 ile sabitlenmiştir, Cargo bağımlılıkları `--locked` kullanır. Sistem Rust'ı, Visual Studio Build Tools veya Horizon Launcher gerekmez. İlk kurulum birkaç GB disk alanı kullanabilir. Bu derleyici şu an Windows x64 içindir ve birlikte yüklü `.hnmod` paketlerinin aynı Horizon ile `horizon-mixins` commit'lerine sabitlenmesini bekler. `.HNMOD` imzası ve kaynak paketi her derleme öncesinde yeniden doğrulanır.

Arka plan görseli kullanmak istersen `background.jpg` dosyasını derlenen uygulamanın yanına koy. Dosya yoksa arayüz kendi degrade arka planını çizer.

## Gerekli araçlar

- CMake 3.21 veya üstü
- C++17 destekli derleyici (Windows için Visual Studio 2022 C++ iş yükü önerilir)
- Qt 6.5 veya üstü: Widgets, Network ve Concurrent bileşenleri (Qt'nin QtZlibPrivate modülü de kurulu olmalı)

Bu klasör CMake kaynak projesidir. Qt Creator'da **File > Open File or Project** ile bu klasördeki `CMakeLists.txt` dosyasını açıp Qt kitini seç. `src/main.cpp` ve `src/launcher_engine.cpp` içindeki özellikler CMake hedefinde derlenir; `.exe` dosyası kaynak değil, CMake derlemesinin çıktısıdır.

Qt kurulumunun yolunu kendi bilgisayarındaki Qt kitine göre değiştir. Proje, Qt kurulumunun içindeki zlib'i kullanır:

```powershell
cmake -S . -B build -DCMAKE_PREFIX_PATH="C:/Qt/6.8.3/msvc2022_64"
cmake --build build --config Release
```

Kurulum, oyunun verilerini Windows'ta `%APPDATA%\SoldierGamingMinecraft\.minecraft` içine yazar. Microsoft oturum açma uygulama kaydı gerektirirse Ayarlar'daki özel istemci kimliği alanı kullanılabilir. Temurin otomatik kurulumu Windows içindir; Java arşivleri `%APPDATA%\SoldierGamingMinecraft\runtimes` altında saklanır. Her profilin oyun verileri ve Java/RAM/JVM seçenekleri ayrı tutulur.

## Kapsam notları

Kaynak, `script8.py` içindeki ana launcher akışlarını C++'a taşır; bu bir Python çeviricisi değildir. ZIP yardımcısı yaygın ZIP/deflate arşivlerini destekler; ZIP64 ve şifreli ZIP dosyaları desteklenmez. Dışa aktarılan profiller ZIP64 kullanmadığından 4 GB sınırı vardır.
