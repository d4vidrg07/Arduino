/* Práctica 2: Timers en el uc Atmega 2560
 *
 * Descripción: Aplicación correspondiente a la práctica 2 (frecuencímetro)
 *
 *  - Contador de 0 a 999 en el display, con pulsadores y teclado.
 *  - El display y el teclado se refrescan con una interrupción del Timer 3 cada 5 ms
 *    (antes se usaba la interrupción externa INT3).
 *  - Menú por el terminal: 1, 2, 3 = formas de ver el contador; 4 = frecuencímetro.
 *  - EP1: 5 = Timer 4 (OC4A 50%, OC4B 10%, OC4C 25%, pulso en pin 13; PLEFT/PRIGHT mueven OC4B)
 *         6 = control de OC4C con PUP/PDOWN y dirección del motor (IO10) con PENTER
 *  - Frecuencímetro: mide la señal que entra por ICP3 (pata 9 del micro).
 *
 * Fichero: 	26-27_plab2_drg
 * Creado		18 octubre 2026
 * Autor:		David Ramos García
*/


// Definición para pulsadores
// PORTC: PC[7:0] --> PINES 30-31-32-33-34-35-36-37
#define PRIGHT  30    //	PC[7] pulsador right
#define PDOWN   31    //	PC[6]		"" 		down
#define PLEFT   32    //	PC[5]		"" 		left
#define PENTER 	33    //	PC[4]		"" 		entrar
#define PUP     34    //	PC[3]		"" 		up
#define SPEAKER 37    // 	PC[0]					speaker


// Definiciones para teclado/cátodos de display (para el manejo de los 4 dígitos del display de forma entrelazada)
// PORTL: PL[7:0] --> PINES 42-43-44-45-46-47-48-49
//														 R1-R2-R3-R4-D1-D2-D3-D4 --> FILAS_TECLADO (R) | CÁTODOS DISPLAY (D)
#define D4    0xFE    // 1111 1110 visualiza 	unidades
#define D3    0xFD    // 1111 1101 		"				decenas
#define D2    0xFB    // 1111 1011 		"				centenas
#define D1    0xF7    // 1111 0111 		"				millares
#define DOFF  0xFF    // 1111 1111 todos apagados: todos los cátados comunes a "1"
#define DON   0xF0    // 1111 0000 todos encendidos: todos los cátodos comunes a "0"
// display
int tabla_7seg[] = {63,06,91,79,102,109,125,39,127,103,0x77,0x7C, 0x39, 0x5E, 0x79,0x71,0xFF}; // 0,1,2... E,F, todos encendidos
// tabla_7seg[5] es el byte que hay que sacar por PORTA para ver un "5"
#define SEG_GUION 0x40		// solo el segmento del medio: se ve "-"

	//TECLADO
	// Mapeo matricial para las teclas del teclado
	char teclado_map[][3] = { {'1','2','3'},
                        {'4','5','6'},
                        {'7','8','9'},
                        {'*','0','#'}};


// ---------------------------------------------------------------------------
// VARIABLES DE LA APLICACIÓN
// ---------------------------------------------------------------------------
int contador = 0;						// número que se enseña (0..999)
int contador_anterior = 0;		// para saber si ha cambiado y hacer sonar el pitido
int paso = 1;							// cuánto suman/restan los pulsadores (1 o 2)
int modo = 1;							// modo del menú (1-6)
bool escribiendo = false;			// true mientras tecleamos un número con el teclado
int num_teclado = 0;				// el número que estamos tecleando

// Las variables que se usan en las interrupciones y en loop() llevan "volatile"
// volatile es que (así el micro sabe que pueden cambiar sin avisar)
//varaible se usa en ISR o en Loop
volatile int digit = 0;				// qué dígito toca encender (0=unidades ... 3=millares)
volatile char tecla_vista = 0;		// tecla detectada en este barrido
volatile char tecla_pulsada = 0;	// tecla nueva lista para que la lea loop()
volatile bool tecla_libre = true;	// true si se soltó la tecla (para no repetirla mientras se mantiene)
volatile int frec_display = 0;		// frecuencia que se enseña en el modo 4 (-1 = fuera de rango)

// Variables del frecuencímetro
volatile unsigned long N = 0;			// cuenta las vueltas del Timer 3 (1 vuelta = 5 ms)
volatile unsigned long N_inicial = 0;	// valor de N en el primer flanco
volatile unsigned int icr_a = 0;		// valor del contador (ICR3) en el primer flanco
volatile long ticks = 0;				// tiempo entre los dos flancos (1 tick = 0,5 us)
volatile byte estado_medida = 0;		// 0 = parado, 1 = esperando 1er flanco,
										// 2 = esperando 2º flanco, 3 = medida lista

// Avisos al compilador de las funciones que se definen más abajo
byte cifra(int pos);
void teclado(int col);
void fin_barrido();
void leer_teclado();
void leer_pulsadores();
void leer_menu();
void imprimir_menu();
void frecuencimetro();
void captura_on();
void captura_off();
void timer_4_on();
void timer_4_off();

/*
 * PUERTOS DEL MICRO (cada letra es un grupo de 8 pines)
 *   A -> display (los segmentos de los dígitos, la "pantalla")
 *   C -> pulsadores y altavoz
 *   L -> teclado (filas) y cátodos (los que eligen qué dígito se enciende)
 *
 * REGISTROS DE CADA PUERTO (la x es la letra del puerto: A, C, L...)
 *   DDRx  -> dirección de cada pin: 1 = salida, 0 = entrada
 *   PORTx -> escribe el valor de salida (en un pin de entrada activa la pull-up)
 *   PINx  -> lee el valor que hay ahora mismo en los pines
 */


void setup() {
	Serial.begin(9600);		// inicialización del canal serie para comunicarnos con:
												// PC --> "Herramientas/monitor serie" en la plataforma Arduino
												// Proteus --> consola en Proteus (VIRTUAL TERMINAL)

	//Programar PORTA de salida. Conexión a los segmentos a-b-c-d-e-f-g-dp del display 7-segmentos)
	DDRA = 0xFF;    					// También se puede expresar en binario con B11111111 (DDRA = B1111111;)

	// Escritura en el PORTA --> Todos los segmentos a "1": encendidos
	PORTA = 0xFF;  					// o también B11111111

	// Programación del PUERTO L: filas_teclado(input) | cátodos (output) --> R1-R2-R3-R4-D1-D2-D3-D4
	DDRL = 0x0F;		// PL[7:4] entradas (filas del teclado: R1-R2-R3-R4)
									// PL[3:0] salidas (cátodos comunes display/columnas del teclado: D1-D2/C3-D3/C2-D4/C1
	PORTL = 0xFF; 	// activación resistencias de pull-up en las entradas de las filas de teclado PL[7:4]; display off


	// Programación del PORTC
	DDRC = B00000001; // 0x01:Todos los bits de entrada menos PC0 que es de salida (speaker)
	PORTC = B11111110;	// 0xFE: activación de todas las resistencias de pull-up excepto la de PC0


	// ----- PROGRAMACIÓN DEL TIMER 3 -----
	// Modo 4 (CTC, TOP = OCR3A), prescaler N = 8, interrupción cada 5 ms (vector TIMER3_COMPA_vect)
	// El timer cuenta a 16 MHz / 8 = 2 MHz (1 tick = 0,5 us) -> 5 ms son 10000 pasos (0 a 9999)
	// La salida OC3A (toggle) tiene frecuencia f = 16 MHz / (2*N*(1+OCR3A)) = 100 Hz

	cli();						// cli() desactivo interrupciones

	// inicialización
	TCCR3A = 0;
	TCCR3B = 0;
	TCCR3C = 0;
	TCNT3 = 0;					// contador a cero
	OCR3A = 9999;				// TOP = 9999 -> una vuelta cada 5 ms
	pinMode(5, 1); 				// la salida OC3A está en el pin 5: programar de salida
	
	//Salidas timer 3
	//OCRA -> pin 5
	//OCRB -> pin 2
	//OCRC -> pin 3

	// TCCR3A: COM3A0 = 1 -> la salida OC3A cambia de nivel (toggle) en cada vuelta
	TCCR3A = B01000000;

	// TCCR3B: ICES3 = 1 -> la captura (frecuencímetro) se hace en el flanco de SUBIDA
	//         WGM32 = 1 -> modo 4 (CTC: cuenta hasta OCR3A y vuelve a 0)
	//         CS31  = 1 -> prescaler N = 8
	TCCR3B = B01001010;

	// TIMSK3: OCIE3A = 1 -> interrupción cada vez que llega a OCR3A (cada 5 ms)
	// (la interrupción de captura ICIE3 solo se activa en el modo 4)
	TIMSK3 = (1 << OCIE3A);

	//---------PROGRAMACION DEL TIMER 4 --------------------
	//QUITADA PARA EL MODO ON Y OF

	sei();						// activar interrupciones

	imprimir_menu();
	}


// ---------------------------------------------------------------------------
// LOOP: aquí solo se atienden las órdenes del usuario.
// El display lo refresca la interrupción del Timer 3 por su cuenta.
// ---------------------------------------------------------------------------
void loop() {

	leer_menu();			// ¿ha elegido una opción por el terminal?
	leer_teclado();			// ¿ha pulsado una tecla del teclado?
	leer_pulsadores();		// ¿ha pulsado algún botón?

	// Pitido cada vez que cambia el contador
	if(contador != contador_anterior){
		tone(SPEAKER, 1000, 50);		// 1000 Hz durante 50 ms
		contador_anterior = contador;
	}
/*
 * MODOS DEL MENÚ (se eligen escribiendo el número en el terminal)
 *
 * Modo | Qué hace                               | Display (D1-D2-D3-D4)
 * -----+----------------------------------------+---------------------------------------
 *  1   | Contador normal, 3 dígitos             | apagado - centenas - decenas - unidades  | 
 *  2   | Contador reducido inferior, 2 dígitos  | apagado - apagado - decenas - unidades	| 
 *  3   | Contador reducido superior, 2 dígitos  | decenas - unidades - apagado - apagado | 
 *  4   | Frecuencímetro                         | 4 dígitos con la frecuencia en Hz ; si 1234Hz -> display 1234
																														si     50Hz -> dsiplay 0050
 *
 * Modos 1 a 3: se ve el contador (0 a 999) y funcionan pulsadores y teclado.
 * Modo 4: se mide la señal de ICP3, el teclado no hace nada. Por encima de
 *         9999 Hz el display muestra "----" y el valor sale por el terminal.
 */


	if(modo == 4){
		frecuencimetro();			//calcula frecuneca y la muestra
	}

  }

ISR(TIMER4_COMPA_vect){
	//ponga 1 en la salida digita de PB7(pin 13) como primera instruaicon y luego a 0
	digitalWrite(13,1);
	digitalWrite(13,0);
	
	
}


// ---------------------------------------------------------------------------
// INTERRUPCIÓN DEL TIMER 3: se ejecuta sola cada 5 ms
// En cada vez enciende UN dígito del display y mira UNA columna del teclado.
// Como pasa muy rápido, el ojo ve los 4 dígitos encendidos a la vez.
// ---------------------------------------------------------------------------
ISR(TIMER3_COMPA_vect){
	PORTL = DOFF;			// apagar todo antes de cambiar de dígito
	PORTA = 0x00;

	N++;					// una vuelta más (lo usa el frecuencímetro)

	switch(digit){
		case 0:				// unidades (columna 1 del teclado)
			PORTA = cifra(0);
			PORTL = D4;			//elige cual se enciende en el display, en este caso unidades
			teclado(0);
			break;
		case 1:				// decenas (columna 2 del teclado)
			PORTA = cifra(1);		
			PORTL = D3;
			teclado(1);
			break;
		case 2:				// centenas (columna 3 del teclado)
			PORTA = cifra(2);
			PORTL = D2;
			teclado(2);
			break;
		case 3:				// millares (no tiene columna de teclado)
			PORTA = cifra(3);
			PORTL = D1;
			fin_barrido();	// se acabó un barrido completo
			break;
	}	
	digit++;
	if(digit > 3) digit = 0;	// digit = posicciones ; si digit 3 millares entonces se vuelve al 0 y pasa a unidades
}


// Devuelve los segmentos que hay que encender en la posición 'pos'
// (0 = unidades, 1 = decenas, 2 = centenas, 3 = millares) según el modo
byte cifra(int pos){
	int valor;			//numero que se va a dibujar en el display

	if(modo == 4){														//si modo = 4 (frecuencia)
		if(frec_display < 0) return SEG_GUION;			// fuera de rango: "----"			; si es negativo guion (error)
		valor = frec_display;										//copias el conteido de frec_display
	} else if(escribiendo){											//si escribes 
		valor = num_teclado;					// mientras tecleo, el valor sera el num tecleado
	} else {
		valor = contador;							//si no, el valor no cambia y vale lo que valga el contador
	}

	// ¿Este dígito debe estar encendido en el modo actual?
	//elegir si el digito deberai estar encendido o apagado
	//pos -> (0 = unidades, 1 = decenas, 2 = centenas, 3 = millares)
	//return 0 es apagarlo, poner la cifra a 0
	if(modo == 1 && pos == 3) return 0;			// modo 1: OFF-centenas-decenas-unidades
	if(modo == 2 && pos >= 2) return 0;			// modo 2: OFF-OFF-decenas-unidades
	if(modo == 3){								// modo 3: decenas-unidades-OFF-OFF
		if(pos < 2) return 0;
		pos = pos - 2;							// D2 = unidades, D1 = decenas ; restas pos porque va de D1 a D4 asi que cuando haces eso
														// pasa de OFF-OFF-DEC-Uds a Dec-Uds -FoF-OFF-
	}

	// Quedarse con la cifra que toca: dividir entre 10 'pos' veces
	//va sacando y devolviendo los valores en orden de uds a millar ; si tienes 347 saca en este orden 7,4,3,0       
	for(int i = 0; i < pos; i++){
		valor = valor / 10;
	}
	return tabla_7seg[valor % 10];
}


// ---------------------------------------------------------------------------
// TECLADO
// El teclado se mira columna a columna. 'col' es la columna que toca mirar
// (0, 1 o 2). Esa columna está a 0 porque su dígito del display está encendido.
// Si hay una tecla pulsada en esa columna, una de las 4 filas también baja a 0.
// ---------------------------------------------------------------------------
//averigua que tecla pulsa el usuario
void teclado(int col){
	int filas = PINL >> 4;		// PINL = R1 R2 R3 R4 | D1 D2 D3 D4. Me quedo solo con las filas
										// (las columnas las pongo yo, ya sé lo que valen)

	// filas vale 15 (1111) si NO hay ninguna tecla pulsada.
	// Si hay una, la fila pulsada vale 0, asi que cogemos los 4 numeros con solo un 0 en la rista y por orden
	if(filas == 7)  tecla_vista = teclado_map[0][col];	// 0111 -> fila 1 pulsada	
	if(filas == 11) tecla_vista = teclado_map[1][col];	// 1011 -> fila 2 pulsada
	if(filas == 13) tecla_vista = teclado_map[2][col];	// 1101 -> fila 3 pulsada
	if(filas == 14) tecla_vista = teclado_map[3][col];	// 1110 -> fila 4 pulsada
}

// Se llama al terminar los 4 dígitos (un barrido completo).
// Sirve para no repetir la misma tecla mientras se mantiene pulsada.
void fin_barrido(){
	if(tecla_vista == 0){				// si en este barrido no se vio ninguna tecla
		tecla_libre = true;				// no hay tecla: ya se soltó y hay tecla libre
	} else if(tecla_libre){					
		tecla_pulsada = tecla_vista;	// tecla nueva: avisar a loop()
		tecla_libre = false;
	}
	tecla_vista = 0;				//para empeza el siguiente barrido
}		

// Lo que hace loop() con la tecla pulsada
void leer_teclado(){
	if(tecla_pulsada == 0) return;		// si no hay tecla no lees y sales de la funcion 

	char tecla = tecla_pulsada;			//paso la variable a una local para trabajar con ella 
	tecla_pulsada = 0;						//y la anterior a 0 para reiniciar y no procesarla 2 veces 

	if(modo == 4) return;				// solos los modos del 1-3 tecletean , el 4 es el frecuencímetro que mide frecuencia

	if(tecla == '#'){					// '#' = aceptar el número tecleado es como un Enter, sirve para terminar y guardarlo en display
		if(escribiendo){
			contador = num_teclado;
			if(contador > 999) contador = 999;		//para no pasarte del maximo 
			escribiendo = false;								// ya no estoy tecleando: el display vuelve a enseñar el contador
		}
		num_teclado = 0;
	} else if(tecla == '*'){			// '*' = cancelar
		num_teclado = 0;
		escribiendo = false;
	} else if(tecla >= '0' && tecla <= '9'){			// si la tecla es un dígito (0-9)
		if(num_teclado < 100){							// máximo 3 cifras
			num_teclado = (num_teclado * 10) + (tecla - '0');			
			//num_teclado es el número que llevas montado hasta ahora y va 1 a 1.
			//si es 347 [seria (0*10) + 3] + [(3*10) + 4] + [(34*10) + 7] = 347
			//(tecla - '0') -> '7' - '0'  =  55 - 48  =  7 ; para restar un char
		}
		escribiendo = true;			//escribiendo = true -> muestra num_teclado
												//escribiendo = false -> muestra contador
	}
}


// ---------------------------------------------------------------------------
// PULSADORES
// Cómo funciona (explicado fácil):
//  - Los botones valen 1 cuando están SUELTOS y 0 cuando están PULSADOS.
//  - Cada botón tiene una variable "ant_..." donde apuntamos cómo estaba la última vez.
//  - Si antes estaba suelto (1) y AHORA está pulsado (0), es que se acaba de pulsar:
//    ahí hacemos la acción UNA sola vez (aunque mantengas el botón apretado).
//  - Al final de cada botón guardamos "ahora" en "ant_..." para la próxima vuelta de loop().
//  - "static" hace que las variables ant_... NO se borren al salir de la función,
//    así se acuerdan de su valor la siguiente vez que se llama.
// ---------------------------------------------------------------------------
void leer_pulsadores(){
	static int ant_up = 1, ant_down = 1, ant_left = 1, ant_right = 1, ant_enter = 1;
	int ahora;		// aquí guardamos cómo está el botón en este momento

	// Botón DERECHA: modo 5 -> disminuye el ancho de OC4B | resto de modos -> paso = 2
	ahora = digitalRead(PRIGHT);							// leer el botón (1 = suelto, 0 = pulsado)
	if(ant_right == 1 && ahora == 0){						// antes suelto y ahora pulsado
		if(modo == 5){
			if(OCR4B > 0) OCR4B -= 200;						// -10% de TOP (TOP = 2000), tope en 0
		}
		else{
			paso = 2;
		}
	}
	ant_right = ahora;										// apuntar el estado para la próxima vez

	// Botón IZQUIERDA: modo 5 -> aumenta el ancho de OC4B | resto de modos -> paso = 1
	ahora = digitalRead(PLEFT);
	if(ant_left == 1 && ahora == 0){
		if(modo == 5){
			if(OCR4B < 2000) OCR4B += 200;					// +10% de TOP, tope en TOP (100%)
		}
		else{
			paso = 1;
		}
	}
	ant_left = ahora;

	// Botón ARRIBA: modo 6 -> aumenta el ancho de OC4C | resto de modos -> suma al contador
	ahora = digitalRead(PUP);
	if(ant_up == 1 && ahora == 0){
		if(modo == 6){
			if(OCR4C < 2000) OCR4C += 200;					// +10% de TOP, tope 100%
		}
		else{
			contador = contador + paso;						// sumar 1 o 2 según el paso
			if(contador > 999) contador = 0;				// pasa de 999 a 0
		}
	}
	ant_up = ahora;

	// Botón ABAJO: modo 6 -> disminuye el ancho de OC4C | resto de modos -> resta al contador
	ahora = digitalRead(PDOWN);
	if(ant_down == 1 && ahora == 0){
		if(modo == 6){
			if(OCR4C > 0) OCR4C -= 200;						// -10% de TOP, tope 0%
		}
		else{
			contador = contador - paso;						// restar 1 o 2 según el paso
			if(contador < 0) contador = 999;				// pasa de 0 a 999
		}
	}
	ant_down = ahora;

	// Botón ENTER: modo 6 -> conmuta la dirección del motor (IO10) | resto -> contador a 0
	ahora = digitalRead(PENTER);
	if(ant_enter == 1 && ahora == 0){
		if(modo == 6) digitalWrite(10, !digitalRead(10));	// 0 pasa a 1 y 1 pasa a 0
		else contador = 0;
	}
	ant_enter = ahora;
}


// ---------------------------------------------------------------------------
// MENÚ POR EL TERMINAL
// ---------------------------------------------------------------------------
void leer_menu(){
	if(Serial.available() > 0){				//si hay caracteres
		char op = Serial.read();				//opcion = lo que lees
		if(op >= '1' && op <= '6'){			//si el modo o menu es valido (1-6)
			modo = op - '0';					//convierte el carácter ('3') en número (3) por ejemplo
			escribiendo = false;				// cancelo lo que estuviera tecleando al cambiar de modo
			num_teclado = 0;					//limpiamos lo que estuviese tecleando

			// Modos 5 y 6: Timer 4 encendido | resto de modos: Timer 4 parado
			if(modo == 5 || modo == 6) timer_4_on();
			else timer_4_off();

			// Modo 6: OC4C empieza en el 20% para que los pasos de 200 (10%) cuadren
			if(modo == 6) OCR4C = 400;

			// Al cambiar de modo se para cualquier medida en curso
			captura_off();					// apaga la interrupción de captura
			estado_medida = 0;

			Serial.print("\nModo ");
			Serial.println(op);
		}
	}
}

void imprimir_menu(){
	Serial.println("\n--- MENU DE VISUALIZACION ---");
	Serial.println("1.- Modo normal de visualizacion (tres digitos)");
	Serial.println("2.- Modo reducido-inferior (dos digitos)");
	Serial.println("3.- Modo reducido-superior (dos digitos)");
	Serial.println("4.- Modo frecuencimetro");
	Serial.println("5.- Modo EP1");
	Serial.println("6.- Modo OC4C (PUP/PDOWN ancho, PENTER direccion motor)");
	Serial.print("Entrar opcion: ");
}


//------FUNCIONES DE ACTIVAR Y DESACTIVAR EL TIMER 4 (MODOS 5 Y 6)-------------

void timer_4_on(){
	TCCR4A = 0;
	TCCR4B = 0;
	TCCR4C = 0;
	TCNT4 = 0;						// contador a cero
	ICR4 = 2000;					// TOP = 16 MHz / (2*8*500) = 2000

	OCR4A = 1000;					// OC4A al 50 % de TOP
	OCR4B = 200;					// OC4B al 10 %
	OCR4C = 500;					// OC4C al 25 %

	pinMode(6, 1);					// OC4A
	pinMode(7, 1);					// OC4B
	pinMode(8, 1);					// OC4C
	pinMode(13, 1);					// PB7, para el pulso de la ISR
	pinMode(10, 1);					// IO10: dirección del motor

	TCCR4A = B10101000;				// OC4A, OC4B, OC4C como PWM normal
	TCCR4B = B00010010;				// modo 8 y N = 8
	TIMSK4 = (1 << OCIE4A);			// interrupción TIMER4_COMPA_vect
}


// Para el Timer 4: sin reloj, sin salidas y sin interrupción
void timer_4_off(){
	TCCR4B = 0;						// sin reloj: el contador se para
	TCCR4A = 0;						// OC4A, OC4B y OC4C desconectados
	TIMSK4 = 0;						// sin interrupción
}

// ---------------------------------------------------------------------------
// FRECUENCÍMETRO
// Idea: medir el tiempo (periodo T) entre dos flancos de subida de la señal
// que entra por ICP3 (pata 9). La frecuencia es f = 1 / T.
// El pin ICP3 hace una "foto" del contador (TCNT3) en el registro ICR3 justo
// cuando llega un flanco. Con dos fotos tenemos el tiempo entre ellos.
//
// Para no saturar al micro, NO medimos todo el rato: cada 250 ms activamos la
// captura, cogemos 2 flancos y la apagamos (solo 2 interrupciones por medida).
//
// Rango: con solo (ICR3b - ICR3a) la frecuencia mínima sería 200 Hz (1 vuelta = 5 ms).
// Como contamos vueltas (N), se puede bajar hasta ~1 Hz (timeout de 2 s).
// La frecuencia máxima real la limita el tiempo de las interrupciones (decenas de kHz).
// ---------------------------------------------------------------------------

// Se ejecuta sola cuando llega un flanco de subida a ICP3
ISR(TIMER3_CAPT_vect){
	unsigned int cap = ICR3;		// valor del contador en el flanco
	unsigned long vueltas = N;		// vueltas completas hasta ahora

	// Caso raro: el contador ya dio la vuelta pero la interrupción de 5 ms
	// todavía no se ha ejecutado. Entonces hay que sumar la vuelta a mano.
	if((TIFR3 & (1 << OCF3A)) && cap < 5000){
		vueltas++;
	}

	if(estado_medida == 1){
		// Primer flanco: guardar dónde estamos
		icr_a = cap;
		N_inicial = vueltas;
		estado_medida = 2;
	} else if(estado_medida == 2){
		// Segundo flanco: tiempo = vueltas * 10000 + (ICR3b - ICR3a)  (cada vuelta = 10000 ticks de 0,5 us)
		ticks = (long)(vueltas - N_inicial) * 10000L + (long)cap - (long)icr_a;
		estado_medida = 3;
		captura_off();				// ya tenemos la medida: apagar la captura
	}
}

// Se llama desde loop() cuando estamos en el modo 4
void frecuencimetro(){
	static unsigned long t_inicio = 0;	// cuándo empezó la última medida
	static unsigned long t_print = 0;	// cuándo se imprimió por última vez
	static long frecuencia = 0;			// última frecuencia calculada (Hz)

	if(estado_medida == 0){
		// Parado: si han pasado 250 ms, empezar una medida nueva
		if(millis() - t_inicio >= 250){
			noInterrupts();
			TIFR3 = (1 << ICF3);		// borrar aviso de captura antiguo
			estado_medida = 1;
			captura_on();					// encender la interrupción de captura
			interrupts();
			t_inicio = millis();
		}
	} else if(estado_medida == 3){
		// Medida lista: f = 1 / (ticks * 0,5 us) = 2.000.000 / ticks
		if(ticks > 0){
			frecuencia = 2000000L / ticks;
		}
		estado_medida = 0;
	} else {
		// Midiendo: si en 2 segundos no han llegado los 2 flancos, no hay señal
		if(millis() - t_inicio >= 2000){
			captura_off();
			estado_medida = 0;
			frecuencia = 0;
		}
	}

	// Display: hasta 9999 Hz. Más de eso "----" (se ve en el terminal)
	if(frecuencia <= 9999) frec_display = frecuencia;
	else                   frec_display = -1;

	// Escribir en el terminal cada 500 ms
	if(millis() - t_print >= 500){
		t_print = millis();
		Serial.print("Frecuencia: ");
		Serial.print(frecuencia);
		Serial.println(" Hz");
	}
}

// ---------------------------------------------------------------------------
// ENCENDER / APAGAR LA INTERRUPCIÓN DE CAPTURA
// TIMSK3 es un registro con un "interruptor" (bit) por cada interrupción del Timer 3:
//   bit 1 = OCIE3A -> interrupción de los 5 ms (display y teclado): SIEMPRE encendida
//   bit 5 = ICIE3  -> interrupción de captura (frecuencímetro): solo cuando medimos
// (1 << OCIE3A) = pone un 1 en el bit OCIE3A  |  el símbolo | junta dos interruptores
// ---------------------------------------------------------------------------
void captura_on(){
	TIMSK3 = (1 << OCIE3A) | (1 << ICIE3);		// 5 ms: SÍ | captura: SÍ
}

void captura_off(){
	TIMSK3 = (1 << OCIE3A);						// 5 ms: SÍ | captura: NO (igual que en el setup)
}