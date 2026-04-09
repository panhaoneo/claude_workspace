#ifndef ALERT_H
#define ALERT_H

/* Push a new WARN alert. Called from collector thread (with store locked). */
void alert_push(const char *fmt, ...);

/* Evaluate all alert conditions against current store data.
 * Called at the end of each collector tick (store lock held). */
void alert_evaluate(void);

#endif /* ALERT_H */
