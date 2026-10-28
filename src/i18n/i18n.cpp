/* i18n/i18n.c - EN / AR string tables
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "i18n.h"

static AccLang g_lang = ACC_LANG_AR; /* Arabic first run, mockup parity */

static const gchar *EN[ACC_T_COUNT] = {
  "Welcome, %s", "Everything is under control",
  "Battery", "Current charge", "Design capacity", "Charge cycles",
  "Recommended charge limit", "Battery details",
  "GPU", "GPU usage", "GPU temperature", "Manage GPU mode",
  "Display (OLED)", "Brightness", "Flicker-free software dimming", "Eye-care mode",
  "Active", "Display settings",
  "System info", "Operating system", "RAM", "Internal storage", "General temperature",
  "More details",
  "Fans and cooling", "CPU fan", "GPU fan", "Temperature trend",
  "Quick apps", "Launch now!", "All apps",
  "Home", "Battery Health Charging", "GPU", "Display (OLED)",
  "Power & Performance", "System info", "Apps", "Settings",
  "Coming in a later milestone",
  "Charging", "Discharging", "Full", "Temperature",
  /* M2: functional controls */
  "Performance", "Quiet", "Balanced", "Performance",
  "Now:", "Applies instantly to fans & power limits.",
  "Keyboard backlight", "Off", "Levels 0-3, like the BIOS setting.",
  "Charge limit", "Full (100%)", "Balanced (80%)", "Max lifespan (60%)",
  "Active limit", "None", "Needs polkit authorization; writes the ASUS health-charging file.",
  "Charging policy", "Standard", "Balanced care", "Max lifespan care",
  "GPU mode now:", "Hybrid (iGPU + dGPU)", "Ultimate (dGPU only)",
  "The firmware accepts the switch only while the laptop shuts down, so a new mode starts working after you power on again.",
  "Control helper installed", "Helper not installed — run ./install.sh",
  "Out of date — run ./install.sh",
  "Language", "About", "Version",
  "BIOS", "Kernel", "Panel brightness (read-only)",
  "Applied", "Failed / denied",
  "Battery", "GPU", "Display (OLED)",
  "Status", "Processor",
  "Resolution & refresh",
  "Changes run through polkit; the first change after each login may ask for your password.",
  "Smarter. Calmer. Better.",
  "Built for a better experience",
  "Light mode", "Ultimate",
  "Code editor made for you", "Web browser",
  "Quick terminal access", "File manager",
  "Refresh rate",
  "Intel only (unload NVIDIA until reboot)", "Intel only",
  "Hybrid: the screen stays on the integrated GPU and NVIDIA wakes only when an app needs it. Ultimate: the screen is wired straight to NVIDIA — lower latency, shorter battery life.",
  "Unofficial open-source ASUS laptop control panel for Fedora Linux.",
  "Refresh", "Control helper",
  "NVIDIA driver", "Loaded", "Not loaded", "Battery health",
  "Menu",
  "Hello", "Great to have you here",
  "Serial number", "Copy serial number",
  "CPU load", "Memory", "Hard disk space",
  "Warranty service", "Warranty status is managed on the ASUS website",
  "Open support site",
  "Software updates", "Fedora updates are handled by GNOME Software (dnf).",
  "Open Software",
  "Live readings from your sensors and system",
  "Featured",
  "Toggle light / dark theme",
  "Theme", "Battery Care Mode",
  "Keeps your battery healthy even when it is connected to the charger "
  "for long periods, by limiting how full it charges.",
  "Fan profiles change the system power/cooling behavior only — direct "
  "fan-speed control is not provided by this laptop's firmware.",
  "GPU mode",
  "Cannot unload the NVIDIA driver now: the running desktop session is "
  "holding the GPU open. Log out and retry, or use a text console (TTY).",
  "Authorization dismissed — nothing was changed.",
  "DEVICES", "PREFERENCES",
  "Customer support",
  "Warranty, drivers and official ASUS service links.",
  "Download drivers",
  "Run diagnostics",
  "All checks passed — no issues found.",
  "Some readings need attention.",
  "System diagnosis",
  "Touchpad", "Enable or disable the built-in touchpad.",
  "Connectivity", "Wi-Fi", "Bluetooth", "Not connected", "On", "Off",
  "Service & repair",
  "Check warranty, track a repair, or find an ASUS service center.",
  "Warranty check", "Repair status", "Service locations", "Contact us",
  "E-Reading (grayscale)",
  "Show this app in black and white to reduce color stimuli.",
  "AUDIO & VISUAL", "SYSTEM", "GENERAL",
  "Back",
  "Software dimming",
  "Extra dimming below the panel minimum, powered by Soft Brightness Plus.",
  "Install the Soft Brightness Plus extension to enable software dimming.",
  "Welcome back",
  "Create your account",
  "Sign in to open your device dashboard.",
  "This account stays on this computer only.",
  "Username", "Password", "Confirm password",
  "Remember my username", "Sign in", "Create account",
  "Enter a username and password.",
  "Use at least 4 characters.",
  "The passwords do not match.",
  "Incorrect username or password.",
  "An account already exists on this computer.",
  "Reset local account",
  "Your ASUS laptop, tuned the way you like.",
  "Stored only on this device. No cloud, no telemetry.",
  "Laptop specifications",
  "Device", "Processor", "Memory & storage",
  "Model", "Vendor", "Resolution", "Video memory", "Battery health",
  "Opens a terminal with the full hardware and support report.",
  "Could not open a terminal. Run this command:",
  "App support on this laptop",
  "Supported", "Not available",
  "Checked live from this machine. Nothing here writes to the hardware.",
  "Discrete GPU disable",
  "Night light",
  "Sign out",
  "You stay signed in on this device until you sign out.",
  "Stay signed in",
  "Collapse sidebar",
  "Expand sidebar",
  "The firmware refused this change. This laptop may not offer a switchable GPU mode.",
  "Restart the laptop to apply the new GPU mode.",
  "ASUS firmware refused the switch on battery. Plug in the power adapter and try again.",
  "Applying…",
  "The ASUS service refused the change. Nothing was modified.",
  "Quiet (Whisper), Balanced and Performance are the firmware's fan and power limits.",
  "Active now", "On AC power", "On battery",
  "asusd applies the AC and battery profiles automatically when the power source changes.",
  "Automatic AC/battery switching is off in asusd; only the active profile changes.",
  "Profiles are managed by asusd (asusctl).",
  "asusd is not running, so the profile is written directly to the ACPI interface.",
  "Low power",
  "Enabled now", "After restart",
  "Queued: ASUS applies it during shutdown, then restart to finish.",
  "Queued, but asus-shutdown.service is disabled, so the queue is lost on restart. Run: sudo systemctl enable --now asus-shutdown.service",
  "Integrated only (Eco)",
  "Not available: this firmware exposes no dgpu_disable attribute. Only Hybrid and Ultimate can be switched.",
  "VRAM", "Power draw", "Display output",
  "NVIDIA is driving a display", "Display is wired to the integrated GPU",
  "Switch to Ultimate (panel on NVIDIA)?",
  "Ultimate wires the internal panel straight to the NVIDIA GPU. Battery life drops because the dGPU can no longer sleep, and the firmware only accepts this write once the NVIDIA driver is unloaded — so the change is queued and applied while the laptop shuts down. It takes effect when you power on again.",
  "Cancel", "Switch",
  /* GPU switch explainer */
  "Live", "What happens next",
  "1. The change is queued now. Nothing has switched yet.",
  "2. Shut down or restart. ASUS writes the MUX while the system is shutting down, after the NVIDIA driver is unloaded.",
  "3. Power on. The internal panel is then wired to the NVIDIA GPU.",
  "Cancel queued change",
  "To go back to Hybrid, select it and shut down the same way.",
  "Queued change cancelled. The laptop keeps its current mode.",
  /* CPU priority */
  "CPU priority",
  "Same live profile as Power & Performance — fans and power limits change now.",
};

static const gchar *AR[ACC_T_COUNT] = {
  "أهلاً، %s", "كل شيء تحت السيطرة",
  "البطارية", "السعة الحالية", "السعة التصميمية", "عدد الدورات",
  "حد الشحن الموصى به", "تفاصيل البطارية",
  "GPU", "استخدام GPU", "درجة الحرارة", "إدارة وضع GPU",
  "الشاشة (OLED)", "السطوع", "خفض برمجي بدون وميض", "وضع حماية العين",
  "نشط", "إعدادات الشاشة",
  "معلومات النظام", "نظام التشغيل", "الذاكرة (RAM)", "التخزين الداخلي", "الحرارة العامة",
  "تفاصيل أكثر",
  "المراوح والحرارة", "مروحة المعالج", "مروحة البطاقة الرسومية", "تطور الحرارة",
  "التطبيقات السريعة", "شغّلها الآن!", "جميع التطبيقات",
  "الصفحة الرئيسية", "صحة البطارية", "GPU", "الشاشة (OLED)",
  "الطاقة والأداء", "معلومات النظام", "التطبيقات", "الإعدادات",
  "يأتي في المراحل القادمة",
  "شحن", "تفريغ", "ممتلئة", "درجة الحرارة",
  "الأداء", "هادئ", "متوازن", "أداء",
  "الآن:", "يطبَّق فوراً على المراوح وحدود الطاقة.",
  "إضاءة لوحة المفاتيح", "إيقاف", "أربعة مستويات كما في الإعدادات.",
  "حد الشحن", "كامل (100%)", "متوازن (80%)", "أطول عمر (60%)",
  "الحد المنطبق", "بلا حد", "يحتاج تفويض polkit؛ يكتب ملف العناية بالبطارية.",
  "سياسة الشحن", "قياسي", "عناية متوازنة", "عناية أقصى عمر",
  "وضع GPU الآن:", "هجين (مدمج + منفصل)", "أقصى (المنفصل فقط)",
  "لا تقبل البرمجية الثابتة التبديل إلا أثناء إغلاق الحاسوب، لذا يبدأ الوضع الجديد بالعمل بعد التشغيل التالي.",
  "أداة التحكم جاهزة", "الأداة غير مثبتة — نفّذ ./install.sh",
  "قديمة — نفّذ ./install.sh",
  "اللغة", "حول التطبيق", "الإصدار",
  "البيوس", "النواة", "سطوع اللوحة (للقراءة فقط)",
  "تم التطبيق", "فشل أو رُفض التفويض",
  "البطارية", "البطاقة الرسومية", "الشاشة (OLED)",
  "الحالة", "المعالج",
  "الدقة والتردد",
  "كل تغيير يمر عبر polkit؛ أول تغيير بعد كل تسجيل دخول قد يطلب كلمة السر.",
  "تجربة أذكى. أفضل.",
  "بُنيت من أجل تجربة أفضل",
  "وضع خفيف", "أقصى",
  "محرر الأكواد المناسب لك", "متصفح الويب",
  "وصول سريع للطرفية", "مدير الملفات",
  "معدل التحديث",
  "إنتل فقط (تعطيل إنفيديا حتى إعادة التشغيل)", "إنتل فقط",
  "Hybrid: تبقى الشاشة على كرت الرسوميات المدمج ولا تستيقظ NVIDIA إلا عند حاجة تطبيق إليها. Ultimate: تُوصل الشاشة مباشرةً بـ NVIDIA — استجابة أسرع وعمر بطارية أقصر.",
  "لوحة تحكم غير رسمية مفتوحة المصدر للحاسب الأسوس على فيدورا لينكس.",
  "تحديث", "أداة التحكم",
  "مشغّل إنفيديا", "محمّل", "غير محمّل", "كفاءة البطارية",
  "القائمة",
  "مرحبًا", "رائع أنك هنا",
  "الرقم التسلسلي", "نسخ الرقم التسلسلي",
  "حمل المعالج", "الذاكرة", "مساحة القرص",
  "خدمة الضمان", "يُدار الضمان على موقع أسوس",
  "افتح موقع الدعم",
  "تحديثات البرمجيات", "تتولى فيدورا تحديثات النظام عبر GNOME Software (dnf).",
  "افتح برنامج Software",
  "قراءات مباشرة من المستشعرات والنظام",
  "مُميّز",
  "تبديل المظهر الفاتح / الداكن",
  "المظهر", "وضع العناية بالبطارية",
  "يحافظ على صحة بطاريتك حتى عند توصيل الشاحن لفترات طويلة، عبر تحديد "
  "حدّ شحنها.",
  "أنماط المروحة تغيّر سلوك الطاقة/التبريد للنظام فقط — هذه الحاسوب لا "
  "يوفر تحكمًا مباشرًا في سرعة المروحة من طرفية الجهاز.",
  "نمط المعالج الرسومي",
  "لا يمكن تفريغ مشغّل NVIDIA الآن: جلسة سطح المكتب النشطة تحتفظ "
  "بالبطاقة الرسومية مفتوحة. قم بتسجيل الخروج وأعد المحاولة، أو "
  "استخدم طرفية نصية (TTY).",
  "أُلغي التفويض — لم يتغير شيء.",
  "الأجهزة", "التفضيلات",
  "دعم العملاء",
  "الضمان والتعريفات وروابط خدمة ASUS الرسمية.",
  "تنزيل التعريفات",
  "تشغيل التشخيص",
  "نجحت جميع الفحوصات — لا توجد مشاكل.",
  "بعض القراءات تحتاج إلى انتباه.",
  "تشخيص النظام",
  "لوحة اللمس", "تشغيل أو إيقاف لوحة اللمس المدمجة.",
  "الاتصال", "واي فاي", "بلوتوث", "غير متصل", "قيد التشغيل", "متوقف",
  "الخدمة والإصلاح",
  "تحقق من الضمان، تتبّع عملية إصلاح، أو اعثر على مركز خدمة ASUS.",
  "فحص الضمان", "حالة الإصلاح", "مراكز الخدمة", "اتصل بنا",
  "القراءة الإلكترونية (تدرّج رمادي)",
  "اعرض هذا التطبيق بالأبيض والأسود لتقليل المؤثرات اللونية.",
  "الصوت والصورة", "النظام", "عام",
  "رجوع",
  "تعتيم البرنامج",
  "تعتيم إضافي تحت الحد الأدنى للشاشة عبر Soft Brightness Plus.",
  "ثبّت إضافة Soft Brightness Plus لتفعيل تعتيم البرنامج.",
  "أهلاً بعودتك",
  "أنشئ حسابك",
  "سجّل الدخول لفتح لوحة تحكم جهازك.",
  "هذا الحساب محفوظ على هذا الكمبيوتر فقط.",
  "اسم المستخدم", "كلمة المرور", "تأكيد كلمة المرور",
  "تذكّر اسم المستخدم", "تسجيل الدخول", "إنشاء حساب",
  "أدخل اسم المستخدم وكلمة المرور.",
  "استخدم 4 أحرف على الأقل.",
  "كلمتا المرور غير متطابقتين.",
  "اسم المستخدم أو كلمة المرور غير صحيحة.",
  "يوجد حساب بالفعل على هذا الكمبيوتر.",
  "إعادة تعيين الحساب المحلي",
  "حاسوب ASUS الخاص بك، مضبوط كما تحب.",
  "محفوظ على هذا الجهاز فقط. دون سحابة أو تتبّع.",
  "مواصفات الحاسوب",
  "الجهاز", "المعالج", "الذاكرة والتخزين",
  "الطراز", "الشركة المصنّعة", "الدقة", "ذاكرة الرسوميات", "صحة البطارية",
  "يفتح نافذة طرفية بتقرير كامل عن العتاد والدعم.",
  "تعذّر فتح الطرفية. شغّل هذا الأمر:",
  "دعم التطبيق على هذا الحاسوب",
  "مدعوم", "غير متاح",
  "يُفحص مباشرة من هذا الجهاز. لا شيء هنا يكتب على العتاد.",
  "تعطيل كرت الرسوميات المنفصل",
  "إضاءة ليلية",
  "تسجيل الخروج",
  "تبقى مسجّلاً على هذا الجهاز حتى تسجّل الخروج.",
  "البقاء مسجّلاً",
  "طيّ الشريط الجانبي",
  "توسيع الشريط الجانبي",
  "رفضت البرمجية الثابتة هذا التغيير. قد لا يوفّر هذا الحاسوب وضعًا قابلًا للتبديل لبطاقة الرسوميات.",
  "أعد تشغيل الحاسوب لتطبيق وضع بطاقة الرسوميات الجديد.",
  "رفضت البرمجية الثابتة التبديل على البطارية. وصّل الشاحن ثم أعد المحاولة.",
  "جارٍ التطبيق…",
  "رفضت خدمة ASUS التغيير. لم يُعدَّل شيء.",
  "هادئ (Whisper) ومتوازن وأداء هي حدود المراوح والطاقة في البرمجية الثابتة.",
  "المفعّل الآن", "على التيار الكهربائي", "على البطارية",
  "يطبّق asusd وضعي التيار والبطارية تلقائيًا عند تغيّر مصدر الطاقة.",
  "التبديل التلقائي بين التيار والبطارية معطّل في asusd؛ يتغيّر الوضع النشط فقط.",
  "يدير asusd (asusctl) أوضاع الطاقة.",
  "asusd غير مشغّل، لذا يُكتب الوضع مباشرةً في واجهة ACPI.",
  "طاقة منخفضة",
  "المفعّل الآن", "بعد إعادة التشغيل",
  "في الانتظار: تطبّقه ASUS عند الإغلاق، ثم أعد التشغيل لإكماله.",
  "تم وضعه في الانتظار، لكن asus-shutdown.service معطّلة، لذا سيُفقد عند إعادة التشغيل. نفّذ: sudo systemctl enable --now asus-shutdown.service",
  "المدمج فقط (Eco)",
  "غير متاح: لا يوفّر هذا الإصدار من البرمجية الثابتة خاصية dgpu_disable. يمكن التبديل بين Hybrid وUltimate فقط.",
  "ذاكرة الرسوميات", "استهلاك الطاقة", "إخراج الشاشة",
  "بطاقة NVIDIA تقود الشاشة", "الشاشة موصولة بكرت الرسوميات المدمج",
  "التبديل إلى Ultimate (اللوحة على NVIDIA)؟",
  "يوصل Ultimate اللوحة الداخلية مباشرةً ببطاقة NVIDIA. تقلّ مدة البطارية لأن كرت الرسوميات المنفصل لن ينام، والبرمجية الثابتة لا تقبل هذا الكتابة إلا بعد تفريغ مشغّل NVIDIA — لذلك يُوضع التغيير في قائمة الانتظار وتكتبه ASUS أثناء إغلاق الحاسوب، ثم يبدأ مفعوله عند التشغيل التالي.",
  "إلغاء", "تبديل",
  /* GPU switch explainer */
  "مباشر", "ما سيحدث بعد ذلك",
  "١. التغيير في قائمة الانتظار الآن. لم يتبدّل شيء بعد.",
  "٢. أطفئ الحاسوب أو أعد تشغيله. تكتب ASUS قيمة MUX أثناء إغلاق النظام، بعد تفريغ مشغّل NVIDIA.",
  "٣. شغّل الحاسوب. عندها تصبح اللوحة الداخلية موصولة ببطاقة NVIDIA.",
  "إلغاء التغيير المجدول",
  "للعودة إلى Hybrid، اختره ثم أطفئ الحاسوب بالطريقة نفسها.",
  "أُلغي التغيير المجدول. يبقى الحاسوب على وضعه الحالي.",
  /* أولوية المعالج */
  "أولوية المعالج",
  "النمط نفسه المباشر في الطاقة والأداء — تتغيّر المراوح وحدود الطاقة فورًا.",
};

/* count mismatch guard: arrays must both be exactly ACC_T_COUNT long */
G_STATIC_ASSERT (G_N_ELEMENTS (EN) == ACC_T_COUNT);
G_STATIC_ASSERT (G_N_ELEMENTS (AR) == ACC_T_COUNT);

void
acc_i18n_set (AccLang lang)
{
  g_lang = lang;
}

AccLang
acc_i18n_lang (void)
{
  return g_lang;
}

void
acc_i18n_toggle (void)
{
  g_lang = (g_lang == ACC_LANG_AR) ? ACC_LANG_EN : ACC_LANG_AR;
  acc_i18n_save_pref ();
}

/* the language lives in the same settings.conf as the skin, under [ui] */
static gchar *
pref_path (void)
{
  return g_build_filename (g_get_user_config_dir (), "asus-control-center",
                           "settings.conf", NULL);
}

void
acc_i18n_load_pref (void)
{
  gchar *p = pref_path ();
  GKeyFile *kf = g_key_file_new ();
  if (g_key_file_load_from_file (kf, p, G_KEY_FILE_NONE, NULL))
    {
      gchar *v = g_key_file_get_string (kf, "ui", "lang", NULL);
      if (v)
        {
          g_lang = g_strcmp0 (v, "en") == 0 ? ACC_LANG_EN : ACC_LANG_AR;
          g_free (v);
        }
    }
  g_key_file_free (kf);
  g_free (p);
}

void
acc_i18n_save_pref (void)
{
  gchar *dir = g_build_filename (g_get_user_config_dir (),
                                 "asus-control-center", NULL);
  g_mkdir_with_parents (dir, 0700);
  gchar *p = g_build_filename (dir, "settings.conf", NULL);
  GKeyFile *kf = g_key_file_new ();
  g_key_file_load_from_file (kf, p, G_KEY_FILE_NONE, NULL);
  g_key_file_set_string (kf, "ui", "lang",
                         g_lang == ACC_LANG_EN ? "en" : "ar");
  GError *e = NULL;
  if (!g_key_file_save_to_file (kf, p, &e))
    {
      g_printerr ("language pref not saved: %s\n", e ? e->message : "?");
      g_clear_error (&e);
    }
  g_key_file_free (kf);
  g_free (p);
  g_free (dir);
}

const gchar *
acc_tr (AccStr s)
{
  return (g_lang == ACC_LANG_AR ? AR : EN)[s];
}

GtkTextDirection
acc_i18n_dir (void)
{
  return g_lang == ACC_LANG_AR ? GTK_TEXT_DIR_RTL : GTK_TEXT_DIR_LTR;
}
