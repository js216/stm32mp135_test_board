void reset_handler(void) __attribute__((naked, target("arm")));
void vectors(void) __attribute__((naked, section("RESET")));

void fiq_handler(void);
void rsvd_handler(void);
void dabt_handler(void);
void pabt_handler(void);
void svc_handler(void);
void undef_handler(void);
void OTG_IRQHandler(void);
void SecurePhysicalTimer_IRQHandler(void);
void irq_handler(void);
