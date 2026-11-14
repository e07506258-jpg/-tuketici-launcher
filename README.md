# Tüketici Launcher

Qt 6 ve C++ ile yazılmış Minecraft Java Edition launcher'ı. Koyu tema, modern arayüz, Horizon 26.3 Rust istemcisi desteği.

## Kurulum

Releases bölümünden `TuketiciLauncher-Setup-v1.0.exe` dosyasını indirip çalıştır. Başka bir şey kurman gerekmez.

## Özellikler

- Minecraft sürüm manifesti, vanilla kurulum ve Fabric, Forge, NeoForge, Quilt profili kurulumu
- Minecraft sürümünün gerektirdiği Java yoksa resmi Eclipse Temurin/Adoptium API'sinden HTTPS üzerinden indirme ve SHA-256 doğrulaması
- Birden fazla çevrimdışı ve Microsoft hesabı arasında geçiş; Microsoft tokenları Windows'ta DPAPI ile saklama
- Çevrimdışı profil, sürüm ve oyun profili seçimi; profil oluşturma, kopyalama, yeniden adlandırma, silme, içe ve ZIP olarak dışa aktarma
- Modrinth ve CurseForge arama/indirme; mod kartlarında görsel önizleme, proje galerisi; Modrinth ve CurseForge modpack kurulumu
- Mod, resource pack, shader, data pack ve dünya indirme; kurulu modları etkinleştirme, devre dışı bırakma ve silme
- Skin indirme, yerel PNG uygulama, Microsoft hesabına skin yükleme
- Sunucu durum sorgusu, Minecraft ekran görüntüsü galerisi, performans ayarları ve launcher/oyun logları
- Horizon Minecraft Java 26.3 Rust istemcisini resmî GitHub Nightly arşivinden doğrudan indirme, SHA-256 karşılaştırması, kendi Minecraft kurulumundan kaynak hazırlama ve istemciyi başlatma (Horizon Launcher kurulmaz)
- Horizon Rust mod kataloğu: `.hmod` içerik paketlerini ve `.hnmod` yerel Rust kodu paketlerini yükleme; yayımlayıcı Minisign imzasını kontrol etme ve onaydan sonra ayrı Rust istemcisi derleme
- RAM ve Java seçimi; GC seçenekleri, yüksek öncelik ve GPU tercihi

## Güvenlik

Tüm web istekleri ve dosya indirmeleri HTTPS kullanır; HTTP bağlantıları ve HTTPS'ten HTTP'ye yönlendirmeler reddedilir. CurseForge API için Ayarlar sayfasına kendi API anahtarını eklemen gerekir.

`.HNMOD` sandbox içinde çalışmaz: Rust kodu derleme sırasında ve oyun sırasında kullanıcı hesabının izinleriyle çalışır. Her derlemeden önce mod kimlikleri ve bu risk gösterilir.

Arka plan görseli kullanmak istersen `background.jpg` dosyasını uygulamanın yanına koy.

## Kaynak koddan derleme

<details>
<summary>Geliştirici talimatları (normal kullanım için gerekmez)</summary>

- CMake 3.21 veya üstü
- C++17 destekli derleyici (Windows için Visual Studio 2022 C++ iş yükü önerilir)
- Qt 6.5 veya üstü: Widgets, Network ve Concurrent bileşenleri

```powershell
cmake -S . -B build -DCMAKE_PREFIX_PATH="C:/Qt/6.x.x/msvc2022_64"
cmake --build build --config Release
```

</details>

## Lisans

MIT — detaylar için [LICENSE](LICENSE) dosyasına bak.

Horizon 26.3 Rust istemcisi [horizon-rust/horizon](https://github.com/horizon-rust/horizon) projesidir, MIT lisansıyla dağıtılmaktadır.  
Minecraft, Mojang Synergies AB'nin ticari markasıdır. Bu proje Mojang veya Microsoft ile ilişkili değildir.
