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
  "BIOS", "Kernel", "Backlight",
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
  "Device settings", "Preferences",
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
  "Use at least 8 characters.",
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
  "You stay signed in until you sign out or restart the computer.",
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
  "Integrated (Intel Arc)",
  "Not supported on this laptop",
  "VRAM", "Power draw", "Display output",
  "NVIDIA is driving a display", "Display is wired to the integrated GPU",
  "Switch to Ultimate (panel on NVIDIA)?",
  "Ultimate wires the internal panel straight to the NVIDIA GPU. Battery life drops because the NVIDIA GPU can no longer sleep. The switch needs a restart and ASUSCraft checks after the restart whether it took effect. To go back, choose Hybrid and restart again. Keep the power adapter connected.",
  "Cancel", "Switch",
  /* GPU switch explainer */
  "Live", "What happens next",
  "1. asusd holds the request. Nothing has switched yet.",
  "2. Shut down or restart. asus-shutdown tries to write the MUX while the system shuts down.",
  "3. Power on. ASUSCraft then checks whether the firmware switched.",
  "Cancel pending switch",
  "To go back to Hybrid, select it and shut down the same way.",
  "Queued change cancelled. The laptop keeps its current mode.",
  /* CPU priority */
  "CPU priority",
  "Same live profile as Power & Performance — fans and power limits change now.",
  /* sign-in hardening */
  "Too many wrong attempts. Try again in %d seconds.",
  "Resetting the account needs the administrator password.",
  /* Integrated (iGPU only) capability and dGPU power */
  "Integrated (Intel Arc) is not available: this laptop's ASUS firmware has no dGPU power switch (dgpu_disable), only the display MUX. ASUSCraft will not turn the NVIDIA GPU off by other means. In Hybrid the NVIDIA GPU powers itself down when nothing uses it.",
  "The firmware reports a dGPU power switch (dgpu_disable), but it has not been verified on this model, so ASUSCraft does not use it.",
  "supergfxd is running. It manages the same GPU as asusd and can interfere with MUX switching. ASUSCraft does not use it.",
  "Leftover supergfxd setting: /etc/modprobe.d/supergfxd.conf still blacklists the NVIDIA driver although supergfxctl is removed, so the open-source nouveau driver can take the GPU at boot. Remove /etc/modprobe.d/supergfxd.conf and /etc/supergfxd.conf, then rebuild the initramfs (sudo dracut -f).",
  "NVIDIA power", "Asleep (draws almost no power)", "Awake",
  "Awake, kept on by: %s",
  /* MUX request lifecycle */
  "The firmware accepted the switch to %s. Restart to apply it; ASUSCraft checks the result after the restart.",
  "The switch to %s was written, but the firmware did not confirm it. Restart; ASUSCraft checks the result after the restart.",
  "%s is now active. Checked after the restart.",
  "The switch to %s did not take effect. The laptop is still in %s.",
  "Reason from the last session's log: %s",
  "Restart (or shut down and power on) to switch the internal panel.",
  "The firmware refused the direct write, so asusd queued it for shutdown. On this laptop queued switches have failed before; ASUSCraft checks the result after the restart.",
  /* built-in OLED dimming */
  "Turn on OLED dimming",
  "Enable OLED dimming",
  "Darkens the OLED panel below its lowest level without PWM flicker. The app installs the bundled Soft Brightness Plus extension (GPL-3) for your user only.",
  "OLED dimming is installed but switched off.",
  "OLED dimming installed. Log out and back in, then use the slider.",
  "OLED dimming enabled. If the slider does nothing yet, log out and back in.",
  "Could not set up OLED dimming. See the terminal for details.",
  /* ASUSCraft sections */
  "Customization", "Hardware", "System Diagnosis", "Updates & Support",
  "Tune performance, battery care, graphics and display.",
  "Live CPU, GPU, memory, storage and thermal details.",
  "Check sensors and services, then run a real diagnostic report.",
  "Software status, drivers and official ASUS support.",
  "Performance", "Battery care", "Graphics", "Display",
  "Device", "Processor", "Graphics", "Memory", "Storage",
  "Battery health", "Cooling", "Display",
  "Current load", "Used", "Unavailable on this laptop",
  "Service status", "asusd", "supergfxd", "Control helper",
  "Running", "Stopped", "Installed", "Missing",
  "Diagnostic report", "Run a check to read the current state.",
  "Battery health check", "Thermal check", "Storage check",
  "Memory check", "Fan check", "Service check",
  "ASUSCraft", "System updates",
  "Install Fedora and firmware updates with GNOME Software (dnf).",
  "Start at login", "Open ASUSCraft automatically when you sign in.",
  "Notifications", "Show confirmations after hardware changes.",
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
  "البيوس", "النواة", "الإضاءة الخلفية",
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
  "إعدادات الجهاز", "التفضيلات",
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
  "استخدم 8 أحرف على الأقل.",
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
  "تبقى مسجّلاً حتى تسجّل الخروج أو تعيد تشغيل الحاسوب.",
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
  "المدمج (Intel Arc)",
  "غير مدعوم على هذا الحاسوب",
  "ذاكرة الرسوميات", "استهلاك الطاقة", "إخراج الشاشة",
  "بطاقة NVIDIA تقود الشاشة", "الشاشة موصولة بكرت الرسوميات المدمج",
  "التبديل إلى Ultimate (اللوحة على NVIDIA)؟",
  "يوصل Ultimate اللوحة الداخلية مباشرةً ببطاقة NVIDIA. تقلّ مدة البطارية لأن بطاقة NVIDIA لن تنام. يحتاج التبديل إلى إعادة التشغيل، ويتحقق ASUSCraft بعدها من نجاحه. للعودة اختر Hybrid وأعد التشغيل مرة أخرى. أبقِ الشاحن موصولًا.",
  "إلغاء", "تبديل",
  /* GPU switch explainer */
  "مباشر", "ما سيحدث بعد ذلك",
  "١. يحتفظ asusd بالطلب. لم يتبدّل شيء بعد.",
  "٢. أطفئ الحاسوب أو أعد تشغيله. تحاول asus-shutdown كتابة MUX أثناء إغلاق النظام.",
  "٣. شغّل الحاسوب. عندها يتحقق ASUSCraft من تبديل البرمجية الثابتة.",
  "إلغاء التبديل المعلّق",
  "للعودة إلى Hybrid، اختره ثم أطفئ الحاسوب بالطريقة نفسها.",
  "أُلغي التغيير المجدول. يبقى الحاسوب على وضعه الحالي.",
  /* أولوية المعالج */
  "أولوية المعالج",
  "النمط نفسه المباشر في الطاقة والأداء — تتغيّر المراوح وحدود الطاقة فورًا.",
  /* تقوية تسجيل الدخول */
  "محاولات خاطئة كثيرة. حاول مرة أخرى بعد %d ثانية.",
  "إعادة تعيين الحساب تتطلب كلمة مرور المسؤول.",
  /* وضع المدمج فقط وطاقة بطاقة NVIDIA */
  "وضع المدمج (Intel Arc) غير متاح: لا تحتوي البرمجية الثابتة لهذا الحاسوب على مفتاح لإطفاء بطاقة NVIDIA (dgpu_disable)، بل على MUX الشاشة فقط. لن يطفئ ASUSCraft بطاقة NVIDIA بطرق أخرى. في وضع Hybrid تُطفئ بطاقة NVIDIA نفسها عندما لا يستخدمها شيء.",
  "تُظهر البرمجية الثابتة مفتاح إطفاء لبطاقة NVIDIA (dgpu_disable)، لكنه لم يُختبر على هذا الطراز، لذا لا يستخدمه ASUSCraft.",
  "الخدمة supergfxd قيد التشغيل. إنها تدير البطاقة نفسها التي يديرها asusd وقد تتعارض مع تبديل MUX. لا يستخدمها ASUSCraft.",
  "إعداد متبقٍ من supergfxd: الملف ‎/etc/modprobe.d/supergfxd.conf ما زال يحظر مشغّل NVIDIA رغم إزالة supergfxctl، فقد يستولي مشغّل nouveau على البطاقة عند الإقلاع. احذف ‎/etc/modprobe.d/supergfxd.conf و‎/etc/supergfxd.conf ثم أعد بناء initramfs (sudo dracut -f).",
  "طاقة NVIDIA", "نائمة (استهلاك شبه معدوم)", "مستيقظة",
  "مستيقظة، تبقيها نشطة: %s",
  /* دورة طلب تبديل MUX */
  "قبلت البرمجية الثابتة التبديل إلى %s. أعد التشغيل لتطبيقه؛ يتحقق ASUSCraft من النتيجة بعد إعادة التشغيل.",
  "كُتب التبديل إلى %s لكن البرمجية الثابتة لم تؤكده. أعد التشغيل؛ يتحقق ASUSCraft من النتيجة بعد إعادة التشغيل.",
  "الوضع %s مفعّل الآن. تم التحقق بعد إعادة التشغيل.",
  "لم يُطبَّق التبديل إلى %s. ما زال الحاسوب على وضع %s.",
  "السبب من سجل الجلسة السابقة: %s",
  "أعد التشغيل (أو أطفئ الحاسوب ثم شغّله) لتبديل اللوحة الداخلية.",
  "رفضت البرمجية الثابتة الكتابة المباشرة، فوضعها asusd في قائمة انتظار الإغلاق. فشلت التبديلات المؤجلة على هذا الحاسوب من قبل؛ يتحقق ASUSCraft من النتيجة بعد إعادة التشغيل.",
  /* التعتيم المدمج لشاشة OLED */
  "تشغيل تعتيم OLED",
  "تفعيل تعتيم OLED",
  "يُعتّم شاشة OLED تحت أدنى مستوى لها دون وميض PWM. يثبّت التطبيق إضافة Soft Brightness Plus المرفقة (GPL-3) لحسابك فقط.",
  "تعتيم OLED مثبّت لكنه متوقف.",
  "تم تثبيت تعتيم OLED. سجّل الخروج ثم الدخول، ثم استخدم شريط التمرير.",
  "تم تفعيل تعتيم OLED. إذا لم يعمل الشريط بعد، سجّل الخروج ثم الدخول.",
  "تعذّر إعداد تعتيم OLED. راجع الطرفية للتفاصيل.",
  /* ASUSCraft sections */
  "التخصيص", "العتاد", "تشخيص النظام", "التحديثات والدعم",
  "اضبط الأداء والعناية بالبطارية والرسوميات والشاشة.",
  "تفاصيل حيّة عن المعالج والرسوميات والذاكرة والتخزين والحرارة.",
  "افحص المستشعرات والخدمات ثم شغّل تقرير تشخيص حقيقيًا.",
  "حالة البرمجيات والتعريفات ودعم ASUS الرسمي.",
  "الأداء", "العناية بالبطارية", "الرسوميات", "الشاشة",
  "الجهاز", "المعالج", "الرسوميات", "الذاكرة", "التخزين",
  "صحة البطارية", "التبريد", "الشاشة",
  "الحمل الحالي", "المستخدم", "غير متاح على هذا الحاسوب",
  "حالة الخدمات", "asusd", "supergfxd", "مساعد التحكم",
  "قيد التشغيل", "متوقفة", "مثبّت", "غير موجود",
  "تقرير التشخيص", "شغّل الفحص لقراءة الحالة الحالية.",
  "فحص صحة البطارية", "فحص الحرارة", "فحص التخزين",
  "فحص الذاكرة", "فحص المراوح", "فحص الخدمات",
  "ASUSCraft", "تحديثات النظام",
  "ثبّت تحديثات فيدورا والبرمجية الثابتة عبر GNOME Software (dnf).",
  "التشغيل عند تسجيل الدخول", "افتح ASUSCraft تلقائيًا عند تسجيل الدخول.",
  "الإشعارات", "اعرض تأكيدات بعد تغييرات العتاد.",
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
