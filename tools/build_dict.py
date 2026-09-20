#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
LightPDF Offline Dictionary Compiler
Compiles ArabEyes English-Arabic dictionary + specialized academic & engineering lexicons:
  - Computer Architecture & Assembly Language
  - Networks, Internet Protocols, IoT & WSN
  - Machine Learning, Deep Learning & Image Processing
  - Cybersecurity, Cryptography & Hardware Security
  - Quality Assurance (QA) & Software Testing
  - Faculty, Teaching & Accreditation (ABET / NCAAA)

Produces: dict/en-ar.dat (Ultra-compact memory-mapped binary dictionary)
"""

import sys
import os
import struct
import urllib.request
import re

sys.stdout.reconfigure(encoding="utf-8")

# Category codes for LightPDF UI tags
CAT_GENERAL = 0
CAT_ARCHITECTURE = 1
CAT_NETWORKS_IOT = 2
CAT_AI_IMAGE = 3
CAT_CYBERSECURITY = 4
CAT_ACADEMIC_ABET_NCAAA = 5
CAT_QA_TESTING = 6

# ==============================================================================
# Specialized Domain Lexicon Overrides & Additions
# ==============================================================================
SPECIALIZED_TERMS = {
    # --------------------------------------------------------------------------
    # 1. Computer Architecture, Organization & Assembly (CAT_ARCHITECTURE)
    # --------------------------------------------------------------------------
    "instruction set architecture": ("معمارية طاقم التعليمات (ISA)", CAT_ARCHITECTURE),
    "isa": ("معمارية طاقم التعليمات (Instruction Set Architecture)", CAT_ARCHITECTURE),
    "branch prediction": ("التنبؤ بالتفرع / مسار القفز الشرطي", CAT_ARCHITECTURE),
    "branch predictor": ("وحدة التنبؤ بالتفرع", CAT_ARCHITECTURE),
    "cache coherence": ("اتساق الذاكرة المخبأة / تناسق الكاش", CAT_ARCHITECTURE),
    "cache miss": ("إخفاق الذاكرة المخبأة", CAT_ARCHITECTURE),
    "cache hit": ("إصابة الذاكرة المخبأة", CAT_ARCHITECTURE),
    "pipeline hazard": ("تعارض مسار البيانات في خط التوجيه", CAT_ARCHITECTURE),
    "pipeline stall": ("توقف مسار خط المعالجة المجمعة / إدخال فقاعة تأخير", CAT_ARCHITECTURE),
    "pipeline bubble": ("فقاعة خط الأنابيب / دورة فارغة في المعالج", CAT_ARCHITECTURE),
    "pipelining": ("تقنية التوجيه المتتالي / خط المعالجة المجمعة", CAT_ARCHITECTURE),
    "data hazard": ("تعارض البيانات (في خط الأنابيب)", CAT_ARCHITECTURE),
    "structural hazard": ("تعارض هيكلي عتادي", CAT_ARCHITECTURE),
    "control hazard": ("تعارض التحكم / القفز التفرعي", CAT_ARCHITECTURE),
    "superscalar": ("معمارية فائقة التدرج (تنفيذ عدة تعليمات في دورة واحدة)", CAT_ARCHITECTURE),
    "out-of-order execution": ("التنفيذ غير المرتب للتعليمات (OoO)", CAT_ARCHITECTURE),
    "in-order execution": ("التنفيذ المرتب المتتالي للتعليمات", CAT_ARCHITECTURE),
    "tomasulo algorithm": ("خوارزمية توماسولو لتنفيذ التعليمات غير المرتبة", CAT_ARCHITECTURE),
    "reorder buffer": ("مخزن إعادة الترتيب المؤقت (ROB)", CAT_ARCHITECTURE),
    "reservation station": ("محطة الحجز في المعالج", CAT_ARCHITECTURE),
    "speculative execution": ("التنفيذ التخميني", CAT_ARCHITECTURE),
    "translation lookaside buffer": ("مخبأ ترجمة العناوين الافتراضية (TLB)", CAT_ARCHITECTURE),
    "tlb": ("مخبأ ترجمة العناوين الافتراضية (Translation Lookaside Buffer)", CAT_ARCHITECTURE),
    "memory hierarchy": ("التسلسل الهرمي للذاكرة", CAT_ARCHITECTURE),
    "virtual memory": ("الذاكرة الافتراضية", CAT_ARCHITECTURE),
    "page table": ("جدول الصفحات (في إدارة الذاكرة)", CAT_ARCHITECTURE),
    "page fault": ("خطأ تعذر العثور على الصفحة في الذاكرة", CAT_ARCHITECTURE),
    "arithmetic logic unit": ("وحدة الحساب والمنطق (ALU)", CAT_ARCHITECTURE),
    "alu": ("وحدة الحساب والمنطق (Arithmetic Logic Unit)", CAT_ARCHITECTURE),
    "control unit": ("وحدة التحكم في المعالج", CAT_ARCHITECTURE),
    "micro-operation": ("عملية صغرية (Micro-op)", CAT_ARCHITECTURE),
    "microarchitecture": ("المعمارية الصغرية للعتاد", CAT_ARCHITECTURE),
    "register file": ("ملف السجلات في المعالج", CAT_ARCHITECTURE),
    "program counter": ("عداد البرنامج (مؤشر التعليمة الحالية)", CAT_ARCHITECTURE),
    "stack pointer": ("مؤشر قمة المكدس (SP)", CAT_ARCHITECTURE),
    "base pointer": ("مؤشر قاعدة إطار المكدس (BP / FP)", CAT_ARCHITECTURE),
    "stack frame": ("إطار المكدس / إطار الدالة البرمجية", CAT_ARCHITECTURE),
    "calling convention": ("ميثاق استدعاء الدوال وتمرير المعاملات", CAT_ARCHITECTURE),
    "addressing mode": ("نمط العنونة في الأسمبلي", CAT_ARCHITECTURE),
    "addressing modes": ("أنماط العنونة (مباشرة، غير مباشرة، إزاحة)", CAT_ARCHITECTURE),
    "immediate addressing": ("العنونة الفورية / المباشرة بالقيمة", CAT_ARCHITECTURE),
    "indirect addressing": ("العنونة غير المباشرة", CAT_ARCHITECTURE),
    "opcode": ("رمز العملية البرمجية في لغة التجميع", CAT_ARCHITECTURE),
    "operand": ("المعامل / القيمة المُجرى عليها العملية", CAT_ARCHITECTURE),
    "endianness": ("ترتيب ترتيب بايتات الكلمة في الذاكرة", CAT_ARCHITECTURE),
    "little-endian": ("ترتيب البايتات ذو النهاية الصغرى", CAT_ARCHITECTURE),
    "big-endian": ("ترتيب البايتات ذو النهاية الكبرى", CAT_ARCHITECTURE),
    "interrupt vector": ("متجه المقاطعة (جدول عناوين معالجات المقاطعة)", CAT_ARCHITECTURE),
    "interrupt service routine": ("روتين خدمة المقاطعة (ISR)", CAT_ARCHITECTURE),
    "isr": ("روتين خدمة المقاطعة (Interrupt Service Routine)", CAT_ARCHITECTURE),
    "polling": ("الاستطلاع الدوري للحالة العتادية", CAT_ARCHITECTURE),
    "direct memory access": ("الوصول المباشر للذاكرة دون المعالج (DMA)", CAT_ARCHITECTURE),
    "dma": ("الوصول المباشر للذاكرة (Direct Memory Access)", CAT_ARCHITECTURE),
    "risc": ("حاسوب طاقم التعليمات المختزلة (RISC)", CAT_ARCHITECTURE),
    "cisc": ("حاسوب طاقم التعليمات المعقدة (CISC)", CAT_ARCHITECTURE),
    "risc-v": ("ريسك-فايف: معمارية طاقم تعليمات معيارية مفتوحة المصدر", CAT_ARCHITECTURE),
    "mips": ("معمارية ميبس للمعالجات الدقيقة", CAT_ARCHITECTURE),
    "x86": ("معمارية معالجات إنتل إكس 86", CAT_ARCHITECTURE),
    "x86-64": ("معمارية 64-بت المتوافقة مع إكس 86 (x64)", CAT_ARCHITECTURE),
    "arm": ("معمارية معالجات إيه آر إم الموفرة للطاقة", CAT_ARCHITECTURE),
    "vliw": ("كلمة تعليمة طويلة جداً (معمارية حوسبة متوازية)", CAT_ARCHITECTURE),
    "simd": ("تعليمة واحدة لبيانات متعددة (معالجة شعاعية متوازية)", CAT_ARCHITECTURE),
    "mimd": ("تعليمات متعددة لبيانات متعددة (حوسبة متعددة الأنوية)", CAT_ARCHITECTURE),
    "logic gate": ("بوابة منطقية (AND, OR, NOT, XOR, NAND)", CAT_ARCHITECTURE),
    "flip-flop": ("قلاب منطقي (دائرة حفظ البت الواحد)", CAT_ARCHITECTURE),
    "latch": ("ماسكة منطقية", CAT_ARCHITECTURE),
    "multiplexer": ("ناخب / متعدد الإرسال (MUX)", CAT_ARCHITECTURE),
    "demultiplexer": ("موزع الإرسال (DEMUX)", CAT_ARCHITECTURE),
    "combinational logic": ("دوائر منطقية توافقية", CAT_ARCHITECTURE),
    "sequential logic": ("دوائر منطقية تتابعية / تعاقبية", CAT_ARCHITECTURE),
    "propagation delay": ("زمن تأخير الانتشار للإشارة المنطقية", CAT_ARCHITECTURE),
    "clock cycle": ("دورة ساعة المعالج", CAT_ARCHITECTURE),
    "clock jitter": ("اضطراب نبضات الساعة", CAT_ARCHITECTURE),
    "setup time": ("زمن الاستقرار المطلوب للبيانات قبل نبضة الساعة", CAT_ARCHITECTURE),
    "hold time": ("زمن التثبيت المطلوب للبيانات بعد نبضة الساعة", CAT_ARCHITECTURE),

    # --------------------------------------------------------------------------
    # 2. Networks, Internet Protocols, IoT & WSN (CAT_NETWORKS_IOT)
    # --------------------------------------------------------------------------
    "throughput": ("معدل النقل الفعلي للبيانات / الإنتاجية القصوى", CAT_NETWORKS_IOT),
    "latency": ("زمن التأخير / الكمون في انتقال الإشارة", CAT_NETWORKS_IOT),
    "jitter": ("تقلب زمن التأخير / التباين الزمني للحزم", CAT_NETWORKS_IOT),
    "packet loss": ("فقدان الحزم الشبكية", CAT_NETWORKS_IOT),
    "bandwidth": ("عرض النطاق الترددي للشبكة", CAT_NETWORKS_IOT),
    "encapsulation": ("تغليف البيانات عبر طبقات الشبكة", CAT_NETWORKS_IOT),
    "decapsulation": ("فك تغليف البيانات الترويسات", CAT_NETWORKS_IOT),
    "subnetting": ("تقسيم الشبكة إلى شبكات فرعية", CAT_NETWORKS_IOT),
    "subnet mask": ("قناع الشبكة الفرعية", CAT_NETWORKS_IOT),
    "cidr": ("التوجيه بين النطاقات غير المصنفة (CIDR)", CAT_NETWORKS_IOT),
    "sliding window": ("النافذة المنزلقة للتحكم في تدفق البيانات", CAT_NETWORKS_IOT),
    "flow control": ("التحكم في تدفق البيانات بين المرسل والمستقبل", CAT_NETWORKS_IOT),
    "congestion control": ("التحكم في ازدحام واختناق الشبكة", CAT_NETWORKS_IOT),
    "congestion avoidance": ("تجنب الازدحام الشبكي", CAT_NETWORKS_IOT),
    "three-way handshake": ("المصافحة الثلاثية لبدء اتصال TCP (SYN, SYN-ACK, ACK)", CAT_NETWORKS_IOT),
    "maximum transmission unit": ("أقصى وحدة إرسال للحزمة (MTU)", CAT_NETWORKS_IOT),
    "mtu": ("أقصى وحدة إرسال للحزمة (Maximum Transmission Unit)", CAT_NETWORKS_IOT),
    "socket": ("مقبس الاتصال الشبكي (IP + Port)", CAT_NETWORKS_IOT),
    "port forwarding": ("إعادة توجيه المنافذ", CAT_NETWORKS_IOT),
    "routing table": ("جدول التوجيه الشبكي", CAT_NETWORKS_IOT),
    "routing protocol": ("بروتوكول التوجيه الشبكي (مثل OSPF, BGP, RIP)", CAT_NETWORKS_IOT),
    "routing protocols": ("بروتوكولات التوجيه الشبكي في الشبكات", CAT_NETWORKS_IOT),
    "ad hoc network": ("شبكة لاسلكية مخصصة ومؤقتة (Ad-Hoc Network)", CAT_NETWORKS_IOT),
    "ad-hoc network": ("شبكة لاسلكية مخصصة ومؤقتة (Ad-Hoc Network)", CAT_NETWORKS_IOT),
    "distance vector": ("خوارزمية متجه المسافة للتوجيه", CAT_NETWORKS_IOT),
    "link state": ("خوارزمية حالة الوصلة للتوجيه", CAT_NETWORKS_IOT),
    "dijkstra algorithm": ("خوارزمية ديكسترا لأقصر مسار توجيه", CAT_NETWORKS_IOT),
    "bgp": ("بروتوكول البوابة الحدودية (Border Gateway Protocol)", CAT_NETWORKS_IOT),
    "ospf": ("بروتوكول فتح أقصر مسار أولاً (OSPF)", CAT_NETWORKS_IOT),
    "rip": ("بروتوكول معلومات التوجيه (Routing Information Protocol)", CAT_NETWORKS_IOT),
    "wireless sensor network": ("شبكة مستشعرات لاسلكية (WSN)", CAT_NETWORKS_IOT),
    "wsn": ("شبكة مستشعرات لاسلكية (Wireless Sensor Network)", CAT_NETWORKS_IOT),
    "sensor node": ("عقدة استشعار لاسلكية", CAT_NETWORKS_IOT),
    "mote": ("عقدة استشعار صغرية في شبكات WSN", CAT_NETWORKS_IOT),
    "sink node": ("عقدة المصب / العقدة المجمعة للبيانات", CAT_NETWORKS_IOT),
    "base station": ("المحطة الأساسية المجمعة للبيانات", CAT_NETWORKS_IOT),
    "duty cycle": ("دورة التشغيل النشط / نسبة وقت العمل إلى السكون", CAT_NETWORKS_IOT),
    "energy harvesting": ("حصاد الطاقة البيئية (الشمسية/الحرارية/الاهتزازية)", CAT_NETWORKS_IOT),
    "sleep mode": ("وضع السكون الموفر للطاقة", CAT_NETWORKS_IOT),
    "data aggregation": ("تجميع ودمج البيانات لتقليل الإرسال اللاسلكي", CAT_NETWORKS_IOT),
    "hop count": ("عدد القفزات الشبكية بين العقد", CAT_NETWORKS_IOT),
    "multi-hop": ("التوجيه اللاسلكي متعدد القفزات", CAT_NETWORKS_IOT),
    "topology": ("طوبولوجيا / هيكلية ترابط الشبكة", CAT_NETWORKS_IOT),
    "mesh network": ("شبكة شبكية متداخلة متكافئة", CAT_NETWORKS_IOT),
    "star topology": ("طوبولوجيا نجمية", CAT_NETWORKS_IOT),
    "internet of things": ("إنترنت الأشياء (IoT)", CAT_NETWORKS_IOT),
    "iot": ("إنترنت الأشياء (Internet of Things)", CAT_NETWORKS_IOT),
    "mqtt": ("بروتوكول نقل القياس عن بعد لنظام النشر/الاشتراك في IoT", CAT_NETWORKS_IOT),
    "coap": ("بروتوكول التطبيقات المقيدة للأجهزة محدودة الموارد", CAT_NETWORKS_IOT),
    "lorawan": ("شبكة لورا واسعة النطاق ومنخفضة استهلاك الطاقة (LoRaWAN)", CAT_NETWORKS_IOT),
    "zigbee": ("معيار زيغبي للاتصالات اللاسلكية منخفضة الطاقة (IEEE 802.15.4)", CAT_NETWORKS_IOT),
    "ble": ("بلوتوث منخفض الطاقة (Bluetooth Low Energy)", CAT_NETWORKS_IOT),
    "rfid": ("تقنية تحديد الهوية بموجات الراديو (RFID)", CAT_NETWORKS_IOT),
    "edge computing": ("الحوسبة الطرفية / معالجة البيانات عند حافة الشبكة", CAT_NETWORKS_IOT),
    "fog computing": ("الحوسبة الضبابية الوسيطة بين الطرفيات والسحابة", CAT_NETWORKS_IOT),
    "actuator": ("المشغل الميكانيكي / المحرك المنفذ للأوامر", CAT_NETWORKS_IOT),
    "transducer": ("محول طاقة المستشعر (فيزيائي إلى إشارة كهربائية)", CAT_NETWORKS_IOT),

    # --------------------------------------------------------------------------
    # 3. Machine Learning, Deep Learning & Image Processing (CAT_AI_IMAGE)
    # --------------------------------------------------------------------------
    "machine learning": ("تعلم الآلة", CAT_AI_IMAGE),
    "deep learning": ("التعلم العميق", CAT_AI_IMAGE),
    "neural network": ("شبكة عصبية اصطناعية", CAT_AI_IMAGE),
    "convolutional neural network": ("شبكة عصبية التفافية (CNN)", CAT_AI_IMAGE),
    "cnn": ("شبكة عصبية التفافية (Convolutional Neural Network)", CAT_AI_IMAGE),
    "recurrent neural network": ("شبكة عصبية متكررة (RNN)", CAT_AI_IMAGE),
    "rnn": ("شبكة عصبية متكررة (Recurrent Neural Network)", CAT_AI_IMAGE),
    "lstm": ("ذاكرة المدى الطويل والقصير في الشبكات المتكررة", CAT_AI_IMAGE),
    "transformer": ("محول الانتباه / بنية ترانسفورمر العصبية", CAT_AI_IMAGE),
    "attention mechanism": ("آلية الانتباه في معالجة التسلسلات", CAT_AI_IMAGE),
    "self-attention": ("الانتباه الذاتي الداخلي للرموز", CAT_AI_IMAGE),
    "backpropagation": ("خوارزمية الانتشار العكسي للخطأ", CAT_AI_IMAGE),
    "gradient descent": ("خوارزمية الانحدار التدريجي", CAT_AI_IMAGE),
    "stochastic gradient descent": ("الانحدار التدريجي العشوائي (SGD)", CAT_AI_IMAGE),
    "sgd": ("الانحدار التدريجي العشوائي (Stochastic Gradient Descent)", CAT_AI_IMAGE),
    "learning rate": ("معدل التعلم في تحديث الأوزان", CAT_AI_IMAGE),
    "loss function": ("دالة الخسارة / دالة الهدف التقديرية", CAT_AI_IMAGE),
    "cost function": ("دالة التكلفة الإجمالية للنموذج", CAT_AI_IMAGE),
    "cross-entropy": ("الاعتلاج المتبادل / الإنتروبيا المتقاطعة", CAT_AI_IMAGE),
    "activation function": ("دالة التنشيط العصبي (ReLU, Sigmoid, Softmax)", CAT_AI_IMAGE),
    "relu": ("دالة الوحدة الخطية المصححة (Rectified Linear Unit)", CAT_AI_IMAGE),
    "softmax": ("دالة التنعيم الإحصائي للاحتمالات (Softmax)", CAT_AI_IMAGE),
    "overfitting": ("فرط المطابقة / فرط التخصيص لبيانات التدريب", CAT_AI_IMAGE),
    "underfitting": ("قصور المطابقة لبيانات التدريب", CAT_AI_IMAGE),
    "regularization": ("التنظيم / تسوية الأوزان لمنع فرط المطابقة", CAT_AI_IMAGE),
    "dropout": ("الإسقاط العشوائي للعقد العصبية لمنع فرط المطابقة", CAT_AI_IMAGE),
    "hyperparameter": ("المعامل الفوقي / المفرط القابل للضبط مسبقاً", CAT_AI_IMAGE),
    "epoch": ("دورة تدريبية كاملة عبر مجموعة البيانات", CAT_AI_IMAGE),
    "batch size": ("حجم دفعة العينات لكل خطوة تدريب", CAT_AI_IMAGE),
    "latent space": ("الفضاء الكامن / فضاء التمثيل الداخلي الخفي", CAT_AI_IMAGE),
    "embedding": ("التضمين الشعاعي للبيانات / التضمين العددي", CAT_AI_IMAGE),
    "precision": ("الدقة الإيجابية (نسبة التوقعات الإيجابية الصحيحة)", CAT_AI_IMAGE),
    "recall": ("الحساسية / نسبة الاستدعاء للعينات الإيجابية الحقيقية", CAT_AI_IMAGE),
    "f1-score": ("مقياس إف 1 (المتوسط التوافقي للدقة والاستدعاء)", CAT_AI_IMAGE),
    "confusion matrix": ("مصفوفة الالتباس لتقييم التصنيف", CAT_AI_IMAGE),
    "roc curve": ("منحنى خصائص تشغيل المستقبل (ROC)", CAT_AI_IMAGE),
    "auc": ("المساحة تحت منحنى الخصائص (Area Under Curve)", CAT_AI_IMAGE),
    "unsupervised learning": ("التعلم غير الموجه / غير الخاضع لإشراف", CAT_AI_IMAGE),
    "supervised learning": ("التعلم الموجه / الخاضع لإشراف", CAT_AI_IMAGE),
    "reinforcement learning": ("التعلم التعزيزي / القائم على المكافآت", CAT_AI_IMAGE),
    "clustering": ("التجميع العنقودي للبيانات المتشابهة", CAT_AI_IMAGE),
    "image processing": ("معالجة الصور الرقمية", CAT_AI_IMAGE),
    "spatial domain": ("المجال المكاني للصورة", CAT_AI_IMAGE),
    "frequency domain": ("مجال التردد للصورة (تحويل فورييه)", CAT_AI_IMAGE),
    "fourier transform": ("تحويل فورييه لتحليل ترددات الإشارة والصورة", CAT_AI_IMAGE),
    "convolution": ("الالتفاف / طي المصفوفات في معالجة الصور", CAT_AI_IMAGE),
    "kernel": ("نواة الترشيح / مصفوفة الالتفاف (Filter Kernel)", CAT_AI_IMAGE),
    "spatial filtering": ("الترشيح المكاني لتحسين وتنعيم الصور", CAT_AI_IMAGE),
    "low-pass filter": ("مرشح الترددات المنخفضة (لتنعيم وإزالة الضوضاء)", CAT_AI_IMAGE),
    "high-pass filter": ("مرشح الترددات العالية (لإبراز وكشف الحواف)", CAT_AI_IMAGE),
    "edge detection": ("كشف حواف الأجسام في الصورة", CAT_AI_IMAGE),
    "sobel operator": ("عامل سوبل لحساب تدرج الحواف", CAT_AI_IMAGE),
    "laplacian": ("مؤثر لابلاسيان للكشف عن الحواف", CAT_AI_IMAGE),
    "canny edge detector": ("كاشف كاني الأمثل للحواف", CAT_AI_IMAGE),
    "histogram equalization": ("موازنة المدرج التكراري لتحسين تباين الصورة", CAT_AI_IMAGE),
    "segmentation": ("تجزئة وتقسيم عناصر الصورة", CAT_AI_IMAGE),
    "thresholding": ("تحديد العتبة الثنائية لفصل الخلفية", CAT_AI_IMAGE),
    "morphological operations": ("العمليات المورفولوجية الشكلية في معالجة الصور", CAT_AI_IMAGE),
    "dilation": ("التمدد المورفولوجي / توسيع الحواف في الصورة الثنائية", CAT_AI_IMAGE),
    "erosion": ("الحت المورفولوجي / تنحيف الحواف في الصورة الثنائية", CAT_AI_IMAGE),
    "interpolation": ("الاستيفاء الداخلي لتقدير قيم البكسلات عند التحجيم", CAT_AI_IMAGE),
    "bilinear interpolation": ("الاستيفاء الثنائي الخطي", CAT_AI_IMAGE),
    "bicubic interpolation": ("الاستيفاء التكعيبي المزدوج", CAT_AI_IMAGE),

    # --------------------------------------------------------------------------
    # 4. Cybersecurity, Cryptography & Hardware Security (CAT_CYBERSECURITY)
    # --------------------------------------------------------------------------
    "cybersecurity": ("الأمن السيبراني / أمن الفضاء الإلكتروني", CAT_CYBERSECURITY),
    "cryptography": ("علم التعمية والتشفير الرياضي", CAT_CYBERSECURITY),
    "cryptanalysis": ("تحليل وفك الشفرات الرياضية", CAT_CYBERSECURITY),
    "plaintext": ("النص الواضح / النص المجرد غير المشفر", CAT_CYBERSECURITY),
    "ciphertext": ("النص المشفر المعمى", CAT_CYBERSECURITY),
    "symmetric encryption": ("التشفير المتناظر (مفتاح سري مشترك)", CAT_CYBERSECURITY),
    "asymmetric encryption": ("التشفير غير المتناظر (مفتاح عام ومفتاح خاص)", CAT_CYBERSECURITY),
    "public key": ("المفتاح العام (المعلن للمطابقة والتحقق والتشفير)", CAT_CYBERSECURITY),
    "public key cryptography": ("تشفير المفتاح العام / التشفير غير المتناظر", CAT_CYBERSECURITY),
    "hash collision": ("تصادم دالة التجزئة (تطابق مخرجي دالتين لمدخلين مختلفين)", CAT_CYBERSECURITY),
    "private key": ("المفتاح الخاص (السري للتوقيع وفك التشفير)", CAT_CYBERSECURITY),
    "digital signature": ("التوقيع الرقمي لإثبات الهوية وسلامة البيانات", CAT_CYBERSECURITY),
    "non-repudiation": ("عدم التنصل / عدم الإنكار لصاحب التوقيع", CAT_CYBERSECURITY),
    "confidentiality": ("السرية / منع الكشف غير المصرح به", CAT_CYBERSECURITY),
    "integrity": ("سلامة البيانات وتكاملها ومنع التعديل غير المصرح به", CAT_CYBERSECURITY),
    "availability": ("التوافر / جاهزية وإتاحة الخدمة للمصرح لهم", CAT_CYBERSECURITY),
    "cia triad": ("ثالوث أمن المعلومات: السرية، السلامة، التوافر", CAT_CYBERSECURITY),
    "zero trust": ("معمارية انعدام الثقة (التحقق المستمر لجميع الطلبات)", CAT_CYBERSECURITY),
    "zero trust architecture": ("معمارية انعدام الثقة الأمنية", CAT_CYBERSECURITY),
    "least privilege": ("مبدأ الحد الأدنى من الصلاحيات المطلوبة", CAT_CYBERSECURITY),
    "defense in depth": ("استراتيجية الدفاع في العمق / الحماية متعددة الطبقات", CAT_CYBERSECURITY),
    "attack surface": ("سطح الهجوم / مساحة التعرض للهجمات", CAT_CYBERSECURITY),
    "threat vector": ("مسار التهديد / وسيلة تنفيذ الهجوم", CAT_CYBERSECURITY),
    "vulnerability": ("ثغرة أمنية / نقطة ضعف برمجية أو عتادية", CAT_CYBERSECURITY),
    "exploit": ("استغلال برمجي للثغرة الأمنية", CAT_CYBERSECURITY),
    "zero-day": ("ثغرة اليوم الصفر (ثغرة غير مكتشفة رسمياً ولا تصحيح لها)", CAT_CYBERSECURITY),
    "zero-day vulnerability": ("ثغرة اليوم الصفر الأمنية", CAT_CYBERSECURITY),
    "buffer overflow": ("فائض المخزن المؤقت / طفح الذاكرة الوسيطة", CAT_CYBERSECURITY),
    "stack overflow": ("طفح مكدس الذاكرة", CAT_CYBERSECURITY),
    "heap spray": ("رش الذاكرة الديناميكية لاستغلال الثغرات", CAT_CYBERSECURITY),
    "remote code execution": ("تنفيذ الأوامر البرمجية عن بُعد (RCE)", CAT_CYBERSECURITY),
    "rce": ("تنفيذ التعليمات البرمجية عن بُعد (Remote Code Execution)", CAT_CYBERSECURITY),
    "privilege escalation": ("تصعيد الصلاحيات بدون تصريح", CAT_CYBERSECURITY),
    "return-oriented programming": ("البرمجة الموجهة بالعائد لتجاوز حماية الذاكرة (ROP)", CAT_CYBERSECURITY),
    "rop": ("البرمجة الموجهة بالعائد (Return-Oriented Programming)", CAT_CYBERSECURITY),
    "side-channel attack": ("هجوم القناة الجانبية (تحليل التسريب الفيزيائي للعتاد)", CAT_CYBERSECURITY),
    "timing attack": ("هجوم التوقيت الزمني لاستنتاج المفاتيح", CAT_CYBERSECURITY),
    "power analysis": ("تحليل استهلاك الطاقة لفك المفاتيح التشفيرية (DPA/SPA)", CAT_CYBERSECURITY),
    "physically unclonable function": ("دالة فيزيائية غير قابلة للاستنساخ لبصمة الشريحة (PUF)", CAT_CYBERSECURITY),
    "puf": ("دالة فيزيائية غير قابلة للاستنساخ (Physically Unclonable Function)", CAT_CYBERSECURITY),
    "hardware trojan": ("حصان طروادة عتادي مدمج في الرقاقة الإلكترونية", CAT_CYBERSECURITY),
    "secure boot": ("الإقلاع الآمن المعتمد على سلسلة الثقة العتادية", CAT_CYBERSECURITY),
    "root of trust": ("جذر الثقة الأمني للعتاد", CAT_CYBERSECURITY),
    "trusted platform module": ("وحدة النظام الأساسي الموثوقة للتشفير العتادي (TPM)", CAT_CYBERSECURITY),
    "tpm": ("وحدة النظام الأساسي الموثوقة (Trusted Platform Module)", CAT_CYBERSECURITY),
    "hardware security module": ("وحدة الأمان العتادية لإدارة المفاتيح الحساسة (HSM)", CAT_CYBERSECURITY),
    "hsm": ("وحدة الأمان العتادية (Hardware Security Module)", CAT_CYBERSECURITY),
    "lightweight cryptography": ("التشفير خفيف الوزن للمستشعرات وأجهزة IoT", CAT_CYBERSECURITY),
    "elliptic curve cryptography": ("تشفير المنحنيات الإهليلجية (ECC)", CAT_CYBERSECURITY),
    "ecc": ("تشفير المنحنيات الإهليلجية (Elliptic Curve Cryptography)", CAT_CYBERSECURITY),
    "rsa": ("خوارزمية آر إس إيه للتشفير غير المتناظر بالمفاتيح العامة", CAT_CYBERSECURITY),
    "aes": ("معيار التشفير المتقدم المتناظر (AES-128 / AES-256)", CAT_CYBERSECURITY),
    "sha-256": ("دالة التجزئة الآمنة بطول 256 بت", CAT_CYBERSECURITY),
    "hash function": ("دالة التجزئة الأحادية الاتجاه", CAT_CYBERSECURITY),
    "hmac": ("رمز التحقق من صحة الرسائل القائم على التجزئة", CAT_CYBERSECURITY),
    "zero-knowledge proof": ("إثبات المعرفة الصفرية دون كشف السر (ZKP)", CAT_CYBERSECURITY),
    "zkp": ("إثبات المعرفة الصفرية (Zero-Knowledge Proof)", CAT_CYBERSECURITY),
    "homomorphic encryption": ("التشفير متماثل الشكل (الحساب على البيانات المشفرة)", CAT_CYBERSECURITY),
    "post-quantum cryptography": ("تشفير ما بعد الحوسبة الكمية المقاوم لخوارزميات كمية", CAT_CYBERSECURITY),
    "pqc": ("تشفير ما بعد الحوسبة الكمية (Post-Quantum Cryptography)", CAT_CYBERSECURITY),
    "man-in-the-middle": ("هجوم الوسيط / رجل في المنتصف (MitM)", CAT_CYBERSECURITY),
    "mitm": ("هجوم الوسيط المتنصت بين طرفي الاتصال (Man-in-the-Middle)", CAT_CYBERSECURITY),
    "denial of service": ("هجوم حجب الخدمة (DoS)", CAT_CYBERSECURITY),
    "dos": ("هجوم حجب الخدمة (Denial of Service)", CAT_CYBERSECURITY),
    "distributed denial of service": ("هجوم حجب الخدمة الموزع (DDoS)", CAT_CYBERSECURITY),
    "ddos": ("هجوم حجب الخدمة الموزع (Distributed Denial of Service)", CAT_CYBERSECURITY),
    "phishing": ("التصيد الاحتيالي لسرقة البيانات", CAT_CYBERSECURITY),
    "spear phishing": ("التصيد الاحتيالي الموجه لشخص أو جهة محددة", CAT_CYBERSECURITY),
    "ransomware": ("برمجيات الفدية الخبيثة لتشفير البيانات وابتزاز الضحية", CAT_CYBERSECURITY),
    "malware": ("برمجيات خبيثة ضارة بالأنظمة", CAT_CYBERSECURITY),
    "trojan horse": ("حصان طروادة (برمجية خبيثة تتنكر كبرنامج نافع)", CAT_CYBERSECURITY),
    "rootkit": ("مجموعة أدوات الجذر لإخفاء الاختراق في نواة النظام", CAT_CYBERSECURITY),
    "botnet": ("شبكة الروبوتات / أجهزة مخترقة تحت سيطرة المهاجم", CAT_CYBERSECURITY),
    "sybil attack": ("هجوم سيبيل (انتحال هويات وهمية في الشبكات الموزعة)", CAT_CYBERSECURITY),
    "blackhole attack": ("هجوم الثقب الأسود في شبكات الاستشعار (إسقاط الحزم)", CAT_CYBERSECURITY),
    "sinkhole attack": ("هجوم الثقب البالوعي في شبكات الاستشعار", CAT_CYBERSECURITY),
    "jamming attack": ("هجوم التشويش اللاسلكي المتعمد على الإشارات", CAT_CYBERSECURITY),
    "firewall": ("جدار الحماية الشبكي", CAT_CYBERSECURITY),
    "intrusion detection system": ("نظام كشف التسلل والاختراق (IDS)", CAT_CYBERSECURITY),
    "ids": ("نظام كشف التسلل والاختراق (Intrusion Detection System)", CAT_CYBERSECURITY),
    "intrusion prevention system": ("نظام منع التسلل والاختراق (IPS)", CAT_CYBERSECURITY),
    "ips": ("نظام منع التسلل والاختراق (Intrusion Prevention System)", CAT_CYBERSECURITY),
    "security operations center": ("مركز العمليات الأمنية لمراقبة الحوادث (SOC)", CAT_CYBERSECURITY),
    "soc": ("مركز العمليات الأمنية (Security Operations Center)", CAT_CYBERSECURITY),
    "siem": ("إدارة معلومات وأحداث الأمان السيبراني (SIEM)", CAT_CYBERSECURITY),
    "penetration testing": ("اختبار الاختراق الأمني لتقييم الحماية", CAT_CYBERSECURITY),
    "pen-test": ("اختبار الاختراق الأمني (Penetration Test)", CAT_CYBERSECURITY),
    "red team": ("الفريق الأحمر الهجومي لمحاكاة التهديدات الواقعية", CAT_CYBERSECURITY),
    "blue team": ("الفريق الأزرق الدفاعي لحماية الأنظمة والتصدي للهجمات", CAT_CYBERSECURITY),
    "digital forensics": ("الأدلة الجنائية الرقمية / التحقيق الجنائي السيبراني", CAT_CYBERSECURITY),
    "chain of custody": ("سلسلة حيازة الأدلة الرقمية لضمان سلامتها القضائية", CAT_CYBERSECURITY),
    "incident response": ("الاستجابة للحوادث الأمنية السيبرانية", CAT_CYBERSECURITY),
    "national cybersecurity authority": ("الهيئة الوطنية للأمن السيبراني (NCA)", CAT_CYBERSECURITY),
    "nca": ("الهيئة الوطنية للأمن السيبراني في المملكة العربية السعودية", CAT_CYBERSECURITY),
    "essential cybersecurity controls": ("الضوابط الأساسية للأمن السيبراني (ECC - NCA)", CAT_CYBERSECURITY),

    # --------------------------------------------------------------------------
    # 5. Faculty, Teaching, Higher Education & Accreditation (CAT_ACADEMIC_ABET_NCAAA)
    # --------------------------------------------------------------------------
    "abet": ("مجلس الاعتماد للهندسة والتكنولوجيا الأمريكي (ABET)", CAT_ACADEMIC_ABET_NCAAA),
    "ncaaa": ("المركز الوطني للتقويم والاعتماد الأكاديمي السعودي (NCAAA)", CAT_ACADEMIC_ABET_NCAAA),
    "accreditation": ("الاعتماد الأكاديمي والمؤسسي", CAT_ACADEMIC_ABET_NCAAA),
    "institutional accreditation": ("الاعتماد المؤسسي للجامعة", CAT_ACADEMIC_ABET_NCAAA),
    "programmatic accreditation": ("الاعتماد البرامجي للتخصص", CAT_ACADEMIC_ABET_NCAAA),
    "student outcomes": ("مخرجات تعلم الطلاب (SOs - ABET 1 to 7)", CAT_ACADEMIC_ABET_NCAAA),
    "student outcome": ("مخرج تعلم الطالب (Student Outcome)", CAT_ACADEMIC_ABET_NCAAA),
    "student learning outcomes": ("مخرجات تعلم الطلبة (SLOs)", CAT_ACADEMIC_ABET_NCAAA),
    "student learning outcome": ("مخرج تعلم الطالب (Student Learning Outcome)", CAT_ACADEMIC_ABET_NCAAA),
    "program educational objectives": ("الأهداف التعليمية للبرنامج الأكاديمي (PEOs)", CAT_ACADEMIC_ABET_NCAAA),
    "peo": ("الهدف التعليمي للبرنامج (Program Educational Objective)", CAT_ACADEMIC_ABET_NCAAA),
    "peos": ("الأهداف التعليمية للبرنامج الأكاديمي (PEOs)", CAT_ACADEMIC_ABET_NCAAA),
    "course learning outcomes": ("مخرجات تعلم المقرر الدراسي (CLOs)", CAT_ACADEMIC_ABET_NCAAA),
    "clo": ("مخرج تعلم المقرر (Course Learning Outcome)", CAT_ACADEMIC_ABET_NCAAA),
    "clos": ("مخرجات تعلم المقرر الدراسي (CLOs)", CAT_ACADEMIC_ABET_NCAAA),
    "intended learning outcomes": ("مخرجات التعلم المستهدفة (ILOs)", CAT_ACADEMIC_ABET_NCAAA),
    "continuous improvement": ("التحسين المستمر في جودة التعليم والاعتماد الأكاديمي", CAT_ACADEMIC_ABET_NCAAA),
    "continuous quality improvement": ("التحسين المستمر للجودة (CQI)", CAT_ACADEMIC_ABET_NCAAA),
    "cqi": ("التحسين المستمر للجودة (Continuous Quality Improvement)", CAT_ACADEMIC_ABET_NCAAA),
    "key performance indicator": ("مؤشر الأداء الرئيسي (KPI)", CAT_ACADEMIC_ABET_NCAAA),
    "key performance indicators": ("مؤشرات الأداء الرئيسية (KPIs - NCAAA)", CAT_ACADEMIC_ABET_NCAAA),
    "kpi": ("مؤشر الأداء الرئيسي (Key Performance Indicator)", CAT_ACADEMIC_ABET_NCAAA),
    "kpis": ("مؤشرات الأداء الرئيسية (Key Performance Indicators)", CAT_ACADEMIC_ABET_NCAAA),
    "benchmarking": ("المقارنة المرجعية المعيارية", CAT_ACADEMIC_ABET_NCAAA),
    "internal benchmarking": ("المقارنة المرجعية الداخلية في المؤسسة", CAT_ACADEMIC_ABET_NCAAA),
    "external benchmarking": ("المقارنة المرجعية الخارجية مع برامج مناظرة", CAT_ACADEMIC_ABET_NCAAA),
    "rubric": ("سلم التقييم / مصفوفة معايير رصد وتصنيف الدرجات", CAT_ACADEMIC_ABET_NCAAA),
    "assessment rubric": ("مصفوفة سلم معايير تقييم الطلاب", CAT_ACADEMIC_ABET_NCAAA),
    "course specifications": ("توصيف المقرر الدراسي وفق نماذج الاعتماد", CAT_ACADEMIC_ABET_NCAAA),
    "course report": ("تقرير المقرر الدراسي الفصلي", CAT_ACADEMIC_ABET_NCAAA),
    "program specifications": ("توصيف البرنامج الأكاديمي", CAT_ACADEMIC_ABET_NCAAA),
    "annual program report": ("التقرير السنوي للبرنامج الأكاديمي (APR)", CAT_ACADEMIC_ABET_NCAAA),
    "self-study report": ("تقرير الدراسة الذاتية للاعتماد البرامجي (SSR)", CAT_ACADEMIC_ABET_NCAAA),
    "ssr": ("تقرير الدراسة الذاتية (Self-Study Report)", CAT_ACADEMIC_ABET_NCAAA),
    "capstone design": ("مشروع التخرج الهندسي الشامل المتكامل", CAT_ACADEMIC_ABET_NCAAA),
    "capstone project": ("مشروع التخرج الهندسي للتتويج الأكاديمي", CAT_ACADEMIC_ABET_NCAAA),
    "industrial advisory board": ("المجلس الاستشاري الصناعي للقسم الهندسي (IAB)", CAT_ACADEMIC_ABET_NCAAA),
    "iab": ("المجلس الاستشاري الصناعي (Industrial Advisory Board)", CAT_ACADEMIC_ABET_NCAAA),
    "field experience": ("الخبرة الميدانية / التدريب التعاوني الميداني", CAT_ACADEMIC_ABET_NCAAA),
    "cooperative training": ("التدريب التعاوني للطلاب في قطاع العمل", CAT_ACADEMIC_ABET_NCAAA),
    "curriculum": ("المنهج الدراسي والخطة الأكاديمية", CAT_ACADEMIC_ABET_NCAAA),
    "curriculum committee": ("لجنة الخطط والمناهج الدراسية", CAT_ACADEMIC_ABET_NCAAA),
    "syllabus": ("الخطة التفصيلية للمقرر وتوزيع المحاضرات", CAT_ACADEMIC_ABET_NCAAA),
    "prerequisite": ("المتطلب الدراسي السابق للمقرر", CAT_ACADEMIC_ABET_NCAAA),
    "corequisite": ("المتطلب الدراسي المتزامن / المترافق للمقرر", CAT_ACADEMIC_ABET_NCAAA),
    "pedagogy": ("علم أصول وطرائق التدريس الجامعي", CAT_ACADEMIC_ABET_NCAAA),
    "active learning": ("التعلم النشط القائم على مشاركة الطلاب", CAT_ACADEMIC_ABET_NCAAA),
    "problem-based learning": ("التعلم القائم على حل المشكلات (PBL)", CAT_ACADEMIC_ABET_NCAAA),
    "proctor": ("مراقب الاختبارات الجامعية", CAT_ACADEMIC_ABET_NCAAA),
    "proctoring": ("مراقبة قاعات الاختبارات الجامعية وضبط النزاهة", CAT_ACADEMIC_ABET_NCAAA),
    "invigilator": ("مراقب الامتحان الأكاديمي", CAT_ACADEMIC_ABET_NCAAA),
    "academic integrity": ("النزاهة الأكاديمية ومكافحة الانتحال", CAT_ACADEMIC_ABET_NCAAA),
    "plagiarism": ("الانتحال والسرقة العلمية الأدبية", CAT_ACADEMIC_ABET_NCAAA),
    "grading": ("رصد وتقييم درجات الطلاب في الاختبارات", CAT_ACADEMIC_ABET_NCAAA),
    "faculty member": ("عضو هيئة التدريس الجامعي", CAT_ACADEMIC_ABET_NCAAA),
    "faculty members": ("أعضاء هيئة التدريس", CAT_ACADEMIC_ABET_NCAAA),
    "assistant professor": ("أستاذ مساعد", CAT_ACADEMIC_ABET_NCAAA),
    "associate professor": ("أستاذ مشارك", CAT_ACADEMIC_ABET_NCAAA),
    "full professor": ("أستاذ دكتور (بروفيسور)", CAT_ACADEMIC_ABET_NCAAA),
    "department head": ("رئيس القسم الأكاديمي", CAT_ACADEMIC_ABET_NCAAA),
    "vice dean": ("وكيل الكلية", CAT_ACADEMIC_ABET_NCAAA),
    "dean": ("عميد الكلية", CAT_ACADEMIC_ABET_NCAAA),
    "colloquium": ("ندوة علمية / حلقة نقاش بحثية أكاديمية", CAT_ACADEMIC_ABET_NCAAA),
    "dissertation": ("أطروحة الدكتوراه البحثية", CAT_ACADEMIC_ABET_NCAAA),
    "thesis": ("رسالة الماجستير البحثية", CAT_ACADEMIC_ABET_NCAAA),
    "peer review": ("التحكيم العلمي للأبحاث والمقررات من الأقران", CAT_ACADEMIC_ABET_NCAAA),
    "formative assessment": ("التقييم البنائي / التكويني المستمر للطلاب", CAT_ACADEMIC_ABET_NCAAA),
    "summative assessment": ("التقييم النهائي الشامل لتحصيل الطلاب", CAT_ACADEMIC_ABET_NCAAA),

    # --------------------------------------------------------------------------
    # 6. Quality Assurance & Software Testing (CAT_QA_TESTING)
    # --------------------------------------------------------------------------
    "quality assurance": ("توكيد وضمان الجودة (QA)", CAT_QA_TESTING),
    "quality control": ("مراقبة وضبط الجودة (QC)", CAT_QA_TESTING),
    "qa": ("توكيد وضمان الجودة (Quality Assurance)", CAT_QA_TESTING),
    "qc": ("ضبط ومراقبة الجودة (Quality Control)", CAT_QA_TESTING),
    "verification": ("التحقق من صحة البناء والتنفيذ وفق المواصفات", CAT_QA_TESTING),
    "validation": ("التصديق على مطابقة النظام لمتطلبات وتوقعات المستخدم", CAT_QA_TESTING),
    "verification and validation": ("التحقق والتصديق الهندسي (V&V)", CAT_QA_TESTING),
    "v&v": ("التحقق والتصديق (Verification and Validation)", CAT_QA_TESTING),
    "regression testing": ("اختبار الانحدار للتحقق من عدم تأثر الوظائف السابقة بالتعديل", CAT_QA_TESTING),
    "regression test": ("اختبار الانحدار البرمجي", CAT_QA_TESTING),
    "compliance": ("الامتثال للمعايير واللوائح التنظيمية", CAT_QA_TESTING),
    "traceability matrix": ("مصفوفة تتبع المتطلبات من البداية للاختبار (RTM)", CAT_QA_TESTING),
    "requirements traceability": ("إمكانية تتبع المتطلبات البرمجية", CAT_QA_TESTING),
    "acceptance criteria": ("معايير القبول والتسليم للنظام", CAT_QA_TESTING),
    "user acceptance testing": ("اختبار قبول المستخدم النهائي (UAT)", CAT_QA_TESTING),
    "uat": ("اختبار قبول المستخدم النهائي (User Acceptance Testing)", CAT_QA_TESTING),
    "root cause analysis": ("تحليل السبب الجذري للخلل أو العيب (RCA)", CAT_QA_TESTING),
    "defect density": ("كثافة العيوب والأخطاء البرمجية", CAT_QA_TESTING),
    "test coverage": ("نسبة تغطية الاختبارات البرمجية للكود", CAT_QA_TESTING),
    "code coverage": ("نسبة تغطية الشيفرة البرمجية في الفحص", CAT_QA_TESTING),
    "unit test": ("اختبار الوحدة البرمجية المنعزلة", CAT_QA_TESTING),
    "unit testing": ("اختبار الوحدات البرمجية المنفردة", CAT_QA_TESTING),
    "integration testing": ("اختبار تكامل وترابط الوحدات البرمجية", CAT_QA_TESTING),
    "system testing": ("اختبار النظام المتكامل الكلي", CAT_QA_TESTING),
    "stress testing": ("اختبار الإجهاد الأقصى لتحمل النظام", CAT_QA_TESTING),
    "load testing": ("اختبار الأحمال العالية واستجابة النظام", CAT_QA_TESTING),
    "static analysis": ("التحليل الاستاتيكي للشيفرة بدون تشغيلها", CAT_QA_TESTING),
    "dynamic analysis": ("التحليل الديناميكي للبرنامج أثناء التشغيل", CAT_QA_TESTING),
    "linting": ("الفحص اللغوي والنحوي الآلي للأخطاء البرمجية", CAT_QA_TESTING),
    "conformance": ("المطابقة والامتثال للمواصفات القياسية", CAT_QA_TESTING),
    "audit": ("التدقيق والمراجعة الرسمية للجودة", CAT_QA_TESTING),
    "internal audit": ("التدقيق الداخلي على الجودة والعمليات", CAT_QA_TESTING),
    "external audit": ("التدقيق الخارجي المحايد للاعتماد", CAT_QA_TESTING),
    "non-conformance": ("عدم المطابقة للمواصفات والمعايير (حالة حيود)", CAT_QA_TESTING),
    "corrective action": ("الإجراء التصحيحي لمعالجة الخلل الجذري (CAPA)", CAT_QA_TESTING),
    "preventive action": ("الإجراء الوقائي لمنع تكرار حدوث الخلل", CAT_QA_TESTING),
}


def download_base_wordlist(cache_path: str) -> str:
    """Downloads ArabEyes database if not already cached locally."""
    if os.path.exists(cache_path) and os.path.getsize(cache_path) > 1000000:
        print(f"Using cached base wordlist: {cache_path}")
        with open(cache_path, "r", encoding="utf-8", errors="ignore") as f:
            return f.read()

    url = "https://raw.githubusercontent.com/usefksa/engAraDictionaryFrom_ArabEyes/master/engAraDictionary.sql"
    print(f"Downloading base ArabEyes lexicon from:\n  {url}...")
    req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0 (LightPDF Compiler)"})
    with urllib.request.urlopen(req) as resp:
        content = resp.read().decode("utf-8", errors="ignore")

    with open(cache_path, "w", encoding="utf-8") as f:
        f.write(content)
    print(f"Cached base wordlist to {cache_path} ({len(content)} characters).")
    return content


def load_user_terms(user_file_path: str) -> dict:
    """Loads custom terms from dict/user_terms.txt if it exists."""
    terms = {}
    if not os.path.exists(user_file_path):
        return terms

    print(f"Loading custom user terms from: {user_file_path}...")
    with open(user_file_path, "r", encoding="utf-8", errors="ignore") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            if "=" in line:
                parts = line.split("=", 1)
                term = parts[0].strip()
                val = parts[1].strip()
                if term and val:
                    terms[term.lower()] = (val, CAT_GENERAL)
    print(f"Loaded {len(terms)} custom user terms.")
    return terms


def compile_binary_dictionary(entries: list, output_dat_path: str):
    """
    Packs dictionary entries into an ultra-fast, memory-mappable binary file:
    Header (32 bytes):
      [0..7]   Magic: b'LPDICT01'
      [8..11]  Entry count (uint32)
      [12..15] Index offset (uint32)
      [16..19] Text pool offset (uint32)
      [20..31] Reserved / padding (12 bytes)
    Index Table (Entry Count * 16 bytes):
      [0..3]   Word offset in text pool (uint32)
      [4..5]   Word length in bytes (uint16)
      [6..7]   Category ID (uint16)
      [8..11]  Definition offset in text pool (uint32)
      [12..13] Definition length in bytes (uint16)
      [14..15] Reserved (uint16)
    Text Pool:
      Continuous UTF-8 bytes of words and definitions.
    """
    print(f"Sorting {len(entries)} entries for O(log N) binary search...")
    # Sort strictly by lowercase word
    entries.sort(key=lambda x: x[0].lower())

    text_pool = bytearray()
    index_records = []

    for word, definition, category in entries:
        word_bytes = word.encode("utf-8")
        def_bytes = definition.encode("utf-8")

        word_off = len(text_pool)
        text_pool.extend(word_bytes)
        word_len = len(word_bytes)

        def_off = len(text_pool)
        text_pool.extend(def_bytes)
        def_len = len(def_bytes)

        # 16-byte record
        rec = struct.pack("<IHHIHH", word_off, word_len, category, def_off, def_len, 0)
        index_records.append(rec)

    header_size = 32
    index_size = len(index_records) * 16
    text_pool_offset = header_size + index_size

    header = struct.pack(
        "<8sIII12x",
        b"LPDICT01",
        len(entries),
        header_size,
        text_pool_offset
    )

    print(f"Writing binary dictionary to: {output_dat_path}...")
    with open(output_dat_path, "wb") as f:
        f.write(header)
        for rec in index_records:
            f.write(rec)
        f.write(text_pool)

    file_size = os.path.getsize(output_dat_path)
    file_size_kb = round(file_size / 1024, 1)
    file_size_mb = round(file_size / (1024 * 1024), 2)
    print(f"\n=======================================================")
    print(f"  DICTIONARY COMPILED SUCCESSFULLY!")
    print(f"  Target File:   {output_dat_path}")
    print(f"  Total Entries: {len(entries):,}")
    print(f"  File Size:     {file_size_kb} KB ({file_size_mb} MB)")
    print(f"=======================================================")


def main():
    base_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
    dict_dir = os.path.join(base_dir, "dict")
    tools_dir = os.path.join(base_dir, "tools")
    os.makedirs(dict_dir, exist_ok=True)

    cache_sql_path = os.path.join(tools_dir, "engAraDictionary.sql")
    user_terms_path = os.path.join(dict_dir, "user_terms.txt")
    output_dat_path = os.path.join(dict_dir, "en-ar.dat")

    # 1. Download/load ArabEyes base
    sql_content = download_base_wordlist(cache_sql_path)

    # 2. Extract base entries
    print("Parsing base entries from SQL dump...")
    pattern = re.compile(r"\(\d+,\s*'([^']*)',\s*'([^']*)'\)")
    raw_matches = pattern.findall(sql_content)
    print(f"Extracted {len(raw_matches):,} base entries.")

    dict_map = {}
    for eng, ara in raw_matches:
        eng = eng.strip()
        ara = ara.strip()
        if eng and ara:
            dict_map[eng.lower()] = (eng, ara, CAT_GENERAL)

    # 3. Layer Specialized Engineering, Academic & Cybersecurity Lexicon
    print(f"Injecting {len(SPECIALIZED_TERMS)} specialized domain definitions...")
    for term, (definition, cat) in SPECIALIZED_TERMS.items():
        dict_map[term.lower()] = (term, definition, cat)

    # 4. Layer Custom User Terms (if user_terms.txt exists)
    user_terms = load_user_terms(user_terms_path)
    for term_lower, (definition, cat) in user_terms.items():
        dict_map[term_lower] = (term_lower, definition, cat)

    # 4b. Generate space/hyphen aliases for phrases containing '-'
    extra_aliases = {}
    for term_lower, (term, definition, cat) in list(dict_map.items()):
        if "-" in term_lower:
            spaced = term_lower.replace("-", " ")
            if spaced not in dict_map and spaced not in extra_aliases:
                extra_aliases[spaced] = (spaced, definition, cat)
        elif " " in term_lower:
            hyphenated = term_lower.replace(" ", "-")
            if hyphenated not in dict_map and hyphenated not in extra_aliases:
                extra_aliases[hyphenated] = (hyphenated, definition, cat)
    dict_map.update(extra_aliases)

    # 5. Create starter user_terms.txt if missing
    if not os.path.exists(user_terms_path):
        with open(user_terms_path, "w", encoding="utf-8") as f:
            f.write("# LightPDF Custom User Lexicon\n")
            f.write("# Add your custom research, faculty, or technical terms below.\n")
            f.write("# Format: English Term = Arabic Translation\n")
            f.write("# Lines starting with '#' are ignored.\n\n")
            f.write("Heterogeneous Architecture = معمارية المعالجات غير المتجانسة\n")
            f.write("Edge-AI = الذكاء الاصطناعي عند حافة الشبكة\n")
            f.write("Zero-Knowledge SNARK = إثبات المعرفة الصفرية المقتضب غير التفاعلي (zk-SNARK)\n")
        print(f"Created template user terms file: {user_terms_path}")

    final_entries = list(dict_map.values())
    compile_binary_dictionary(final_entries, output_dat_path)


if __name__ == "__main__":
    main()
