import os
import logging
import discord
from discord import app_commands
from discord.ext import commands

logging.basicConfig(level=logging.INFO)
TOKEN = os.getenv("DISCORD_TOKEN", "").strip()
GUILD_ID = int(os.getenv("DISCORD_GUILD_ID", "0"))
if not TOKEN or not GUILD_ID:
    raise SystemExit("Set DISCORD_TOKEN and DISCORD_GUILD_ID in Railway Variables")

intents = discord.Intents.default()
intents.members = True
bot = commands.Bot(command_prefix="!", intents=intents)
GUILD = discord.Object(id=GUILD_ID)
ROLES = ["Administrador Nebula", "Desarrollador", "Beta Tester", "Productor", "Comunidad"]
CATEGORIES = {
    "INFORMACION": ["bienvenida", "reglas", "anuncios", "avances-del-desarrollo", "registro-de-cambios"],
    "BETA TESTERS": ["descargas-beta", "activar-licencia", "pruebas-de-audio", "feedback-beta"],
    "SOPORTE": ["abrir-ticket", "errores-reportados", "sugerencias", "errores-solucionados"],
    "COMUNIDAD": ["muestra-tus-sonidos", "charla-productores", "votaciones"],
    "EQUIPO": ["equipo-interno", "registro-tickets"]
}

def overwrites(guild, name):
    if name not in ("BETA TESTERS", "EQUIPO"):
        return None
    result = {guild.default_role: discord.PermissionOverwrite(view_channel=False)}
    visible = ("Administrador Nebula", "Desarrollador", "Beta Tester") if name == "BETA TESTERS" else ("Administrador Nebula", "Desarrollador")
    for role_name in visible:
        role = discord.utils.get(guild.roles, name=role_name)
        if role:
            result[role] = discord.PermissionOverwrite(view_channel=True, send_messages=True, read_message_history=True)
    if guild.me is not None:
        result[guild.me] = discord.PermissionOverwrite(view_channel=True, send_messages=True, read_message_history=True, manage_channels=True, manage_messages=True, manage_threads=True, create_private_threads=True, send_messages_in_threads=True)
    return result

class CloseTicket(discord.ui.View):
    def __init__(self):
        super().__init__(timeout=None)

    @discord.ui.button(label="Cerrar ticket", style=discord.ButtonStyle.danger, custom_id="nebula_ticket_close")
    async def close(self, interaction: discord.Interaction, button: discord.ui.Button):
        thread = interaction.channel
        if not isinstance(thread, discord.Thread) or not thread.name.startswith("ticket-"):
            return await interaction.response.send_message("Solo dentro de un ticket.", ephemeral=True)
        requester = int(thread.name.split("-")[1])
        member = interaction.user
        staff = isinstance(member, discord.Member) and (member.guild_permissions.manage_guild or any(r.name in ("Administrador Nebula", "Desarrollador") for r in member.roles))
        if member.id != requester and not staff:
            return await interaction.response.send_message("No tienes permiso para cerrar este ticket.", ephemeral=True)
        await interaction.response.send_message("Ticket cerrado.")
        await thread.edit(archived=True, locked=True)

class TicketModal(discord.ui.Modal, title="Soporte NEBULA"):
    asunto = discord.ui.TextInput(label="Asunto", max_length=90, min_length=3)
    sistema = discord.ui.TextInput(label="Sistema operativo y DAW", placeholder="Mac / Ableton Live 12", max_length=100)
    detalles = discord.ui.TextInput(label="Pasos para reproducir el error", style=discord.TextStyle.paragraph, max_length=1500)

    async def on_submit(self, interaction: discord.Interaction):
        channel = interaction.channel
        if not isinstance(channel, discord.TextChannel):
            return await interaction.response.send_message("Abre el ticket en el canal de soporte.", ephemeral=True)
        await interaction.response.defer(ephemeral=True)
        try:
            thread = await channel.create_thread(name=f"ticket-{interaction.user.id}", type=discord.ChannelType.private_thread, invitable=False)
            await thread.add_user(interaction.user)
            for name in ("Administrador Nebula", "Desarrollador"):
                role = discord.utils.get(interaction.guild.roles, name=name)
                if role:
                    for person in role.members:
                        try: await thread.add_user(person)
                        except discord.HTTPException: pass
            await thread.send(f"Solicitante: {interaction.user.mention}\n**Asunto:** {self.asunto.value}\n**Sistema/DAW:** {self.sistema.value}\n**Detalles:** {self.detalles.value}", view=CloseTicket())
            await interaction.followup.send(f"Ticket creado: {thread.mention}", ephemeral=True)
        except discord.Forbidden:
            await interaction.followup.send("Faltan permisos para crear hilos privados y añadir miembros.", ephemeral=True)

class TicketPanel(discord.ui.View):
    def __init__(self):
        super().__init__(timeout=None)

    @discord.ui.button(label="Abrir ticket", style=discord.ButtonStyle.primary, custom_id="nebula_ticket_open")
    async def open_ticket(self, interaction: discord.Interaction, button: discord.ui.Button):
        await interaction.response.send_modal(TicketModal())

# Content templates are deliberately truthful about beta readiness and credits.
TOPICS = {
 "bienvenida": "Empieza aqui: presentacion del proyecto NEBULA SYNTH y guia de canales.",
 "reglas": "Normas de convivencia, privacidad, soporte y distribucion beta.",
 "anuncios": "Comunicados oficiales publicados exclusivamente por el equipo.",
 "avances-del-desarrollo": "Progreso verificable de desarrollo, enlaces a GitHub y resultados de pruebas.",
 "registro-de-cambios": "Versiones, correcciones confirmadas, problemas conocidos y fechas.",
 "descargas-beta": "Instaladores VST3/AU de Mac y VST3 de Windows cuando esten verificados.",
 "activar-licencia": "Instrucciones de acceso beta. Nunca publiques claves privadas en un canal.",
 "pruebas-de-audio": "Pruebas con referencias 808, pads, leads, MIDI y estabilidad.",
 "feedback-beta": "Opiniones estructuradas de los productores que prueban NEBULA.",
 "abrir-ticket": "Abre un ticket privado para errores, incidencias o dudas.",
 "errores-reportados": "Resumen anonimo de errores confirmados. No publicar datos privados.",
 "sugerencias": "Propuestas de funciones y mejoras del sintetizador.",
 "errores-solucionados": "Incidencias resueltas con enlace a cambios comprobables.",
 "muestra-tus-sonidos": "Comparte tus creaciones con NEBULA y comenta otras producciones.",
 "charla-productores": "Conversacion entre productores, dudas musicales y experiencias.",
 "votaciones": "Encuestas y prioridades de la comunidad.",
 "equipo-interno": "Coordinacion privada del equipo autorizado.",
 "registro-tickets": "Resumen interno del soporte sin credenciales ni datos sensibles."
}
MESSAGES = {
 "bienvenida": "**BIENVENIDO A NEBULA SYNTH**\n\nNEBULA es un sintetizador en desarrollo para productores. Aqui reunimos novedades, pruebas y propuestas.\n\n**Por donde empezar**\n1. Lee #reglas.\n2. Consulta #anuncios y #avances-del-desarrollo.\n3. Si eres tester, solicita el rol Beta Tester al equipo.\n4. Si necesitas soporte, utiliza #abrir-ticket.\n\nLas descargas y licencias se publicaran cuando sus sistemas esten probados.",
 "reglas": "**REGLAS DE NEBULA SYNTH**\n\n1. Trata a todos con respeto; no acoso ni spam.\n2. No publiques contrasenas, tokens, licencias ni datos personales.\n3. No redistribuyas instaladores beta, claves o archivos privados.\n4. Reporta fallos con version, sistema, DAW, pasos y resultado esperado.\n5. No publiques datos privados de tickets en canales publicos.\n6. El acceso al canal beta no significa que una licencia VST este activa.\n7. Las actualizaciones solo se consideran listas tras sus pruebas.",
 "anuncios": "**ANUNCIOS OFICIALES**\nAqui publicaremos nuevas versiones, apertura de pruebas, mantenimientos y decisiones importantes. Solo el equipo publicara anuncios oficiales.",
 "avances-del-desarrollo": "**DIARIO DE DESARROLLO**\nRegistraremos avances verificables con enlace al commit, compilacion o prueba. El codigo esta en https://github.com/Ohgul/nebula-synth . Una compilacion correcta no equivale a una beta validada.",
 "registro-de-cambios": "**HISTORIAL DE CAMBIOS**\nFormato: version, fecha, novedades, correcciones, limitaciones y enlaces a las compilaciones. No se publican funciones incompletas como terminadas.",
 "descargas-beta": "**DESCARGAS BETA**\nTodavia no hay una distribucion Mac/Windows con licencias de 7 dias validada para testers. Los enlaces oficiales apareceran aqui cuando esten disponibles. No descargues instaladores de terceros.",
 "activar-licencia": "**ACTIVACION BETA**\nLa licencia temporal de siete dias se implementara y comprobara en el propio plugin y su servicio de licencias. El rol Beta Tester de Discord solo controla el acceso a canales. Nunca compartas claves por aqui; abre un ticket privado si tienes problemas.",
 "pruebas-de-audio": "**PLAN DE PRUEBAS**\nPrueba 808, pad largo, lead, sonido percusivo, polifonia, automatizacion y ancho estereo. Indica version del plugin, macOS/Windows, DAW, sample rate y pasos para repetir el resultado.",
 "feedback-beta": "**FEEDBACK DE PRODUCTORES**\nCuéntanos: ¿que sonido recreaste?, ¿que funcion te gusto?, ¿que resulto dificil?, ¿que cambiarias? Adjunta un ejemplo cuando tengas derechos para compartirlo.",
 "errores-reportados": "**ERRORES REPORTADOS**\nAqui se publicaran solo incidencias confirmadas sin datos personales. Para un problema nuevo, abre un ticket en #abrir-ticket.",
 "sugerencias": "**IDEAS PARA NEBULA**\nComparte una funcion, su uso musical y por que seria util. Evita repetir propuestas existentes.",
 "errores-solucionados": "**ERRORES SOLUCIONADOS**\nCada solucion confirmada incluira version y referencia del cambio o prueba. No se marcaran problemas como resueltos sin comprobar.",
 "muestra-tus-sonidos": "**MUESTRA TUS SONIDOS**\nComparte presets, renders y ejemplos musicales que puedas distribuir. Describe el patch y sus parametros relevantes.",
 "charla-productores": "**COMUNIDAD DE PRODUCTORES**\nHabla de tecnicas de sintesis, tus DAW y como usas NEBULA.",
 "votaciones": "**VOTACIONES**\nAqui votaremos sobre prioridades de desarrollo. Los resultados orientan las decisiones, no garantizan entregas.",
 "equipo-interno": "**OPERACION INTERNA**\nCoordina aqui soporte, releases y testers autorizados. No pegues claves privadas ni tokens.",
 "registro-tickets": "**REGISTRO DE SOPORTE**\nUso interno para seguimiento de incidentes. Evita copiar contenidos sensibles de tickets.",
}
CREDITS = ("**CREDITOS Y TRANSPARENCIA — NEBULA SYNTH**\n"
           "Proyecto: NEBULA SYNTH. Codigo fuente: https://github.com/Ohgul/nebula-synth .\n"
           "Desarrollo y colaboradores: pendiente de confirmar antes de atribuir nombres o aportaciones.\n"
           "JUCE y las dependencias externas conservan sus licencias y avisos originales.\n"
           "Este servidor no representa una beta terminada ni garantiza funciones todavia en desarrollo.")

async def ensure_post(channel, marker, text, pin=True):
    # Idempotent announcements; avoids reposting on repeated setup commands.
    async for msg in channel.history(limit=100):
        if msg.author.id == bot.user.id and msg.content.startswith(marker):
            if msg.content != text:
                await msg.edit(content=text)
            return False
    message = await channel.send(text)
    if pin:
        try:
            await message.pin(reason="Informacion oficial NEBULA")
        except discord.Forbidden:
            logging.warning("Missing Manage Messages to pin in %s", channel.name)
    return True

async def provision(guild, publish=True):
    created = posted = 0
    for name in ROLES:
        if discord.utils.get(guild.roles, name=name) is None:
            await guild.create_role(name=name, reason="Configuracion NEBULA")
            created += 1
    for category_name, channels in CATEGORIES.items():
        category = discord.utils.get(guild.categories, name=category_name)
        privacy = overwrites(guild, category_name)
        if category is None:
            category = await guild.create_category(category_name, overwrites=privacy or {})
            created += 1
        elif privacy is not None:
            try:
                await category.edit(overwrites=privacy)
            except discord.Forbidden:
                logging.warning("Cannot edit restricted category %s. Check bot role visibility and channel permissions.", category_name)
                continue
        for channel_name in channels:
            channel = discord.utils.get(category.text_channels, name=channel_name)
            if channel is None:
                channel = await guild.create_text_channel(channel_name, category=category, topic=TOPICS.get(channel_name, ""))
                created += 1
            elif channel.topic != TOPICS.get(channel_name, ""):
                await channel.edit(topic=TOPICS.get(channel_name, ""))
            # Read-only official news; staff roles may still speak.
            if channel_name in ("anuncios", "avances-del-desarrollo", "registro-de-cambios", "errores-solucionados", "descargas-beta", "reglas", "bienvenida"):
                await channel.set_permissions(guild.default_role, send_messages=False, add_reactions=False)
                for staff_name in ("Administrador Nebula", "Desarrollador"):
                    role = discord.utils.get(guild.roles, name=staff_name)
                    if role: await channel.set_permissions(role, send_messages=True)
            # Bot explicitly retains posting rights in read-only and private channels.
            await channel.set_permissions(guild.me, view_channel=True, send_messages=True,
                                          read_message_history=True, embed_links=True,
                                          attach_files=True, manage_messages=True)
            if publish and channel_name in MESSAGES:
                if await ensure_post(channel, MESSAGES[channel_name].split("\n")[0], MESSAGES[channel_name]): posted += 1
    # credits live in a dedicated read-only public channel
    info = discord.utils.get(guild.categories, name="INFORMACION")
    credits = discord.utils.get(info.text_channels, name="creditos") if info else None
    if credits is None:
        credits = await guild.create_text_channel("creditos", category=info, topic="Creditos, colaboradores y transparencia; no atribuir aportes sin confirmacion.")
        created += 1
    await credits.set_permissions(guild.default_role, send_messages=False)
    await credits.set_permissions(guild.me, view_channel=True, send_messages=True, read_message_history=True, manage_messages=True)
    if publish and await ensure_post(credits, "**CREDITOS", CREDITS): posted += 1
    panel = discord.utils.get(guild.text_channels, name="abrir-ticket")
    if publish and panel:
        text = "**SOPORTE NEBULA**\nAbre un ticket privado para informar errores en la beta."
        async for msg in panel.history(limit=100):
            if msg.author.id == bot.user.id and msg.content.startswith("**SOPORTE NEBULA**"):
                break
        else:
            await panel.send(text, view=TicketPanel())
            posted += 1
    return created, posted

@bot.tree.command(name="preparar_servidor", description="Organizar, describir y publicar guias y paneles", guild=GUILD)
@app_commands.checks.has_permissions(administrator=True)
async def preparar_servidor(interaction: discord.Interaction):
    await interaction.response.defer(ephemeral=True)
    try:
        created, posted = await provision(interaction.guild, publish=True)
        await interaction.followup.send(f"NEBULA configurado: {created} elementos creados; {posted} mensajes oficiales publicados. Puedes repetir el comando sin duplicar.", ephemeral=True)
    except discord.Forbidden:
        logging.exception("Insufficient permissions while provisioning")
        await interaction.followup.send("Faltan permisos: Manage Channels, Manage Roles, Manage Messages, Create Private Threads y enviar mensajes. Coloca el rol del bot arriba de los roles que administra.", ephemeral=True)
    except Exception:
        logging.exception("Unexpected setup failure")
        await interaction.followup.send("Hubo un problema al configurar; revisa los registros del bot en Railway.", ephemeral=True)


@bot.tree.command(name="configurar", description="Crear canales y roles de NEBULA", guild=GUILD)
@app_commands.checks.has_permissions(administrator=True)
async def configurar(interaction: discord.Interaction):
    await interaction.response.defer(ephemeral=True)
    guild = interaction.guild
    if guild is None: return
    created, posted = await provision(guild, publish=False)
    await interaction.followup.send(f"Configuracion lista: {created} elementos nuevos. Ejecuta /preparar_servidor para publicar guias y tickets.", ephemeral=True)

@bot.tree.command(name="publicar_panel", description="Publicar botón para abrir tickets", guild=GUILD)
@app_commands.checks.has_permissions(administrator=True)
async def publicar_panel(interaction: discord.Interaction):
    guild = interaction.guild
    channel = discord.utils.get(guild.text_channels, name="abrir-ticket") if guild else None
    if channel is None:
        return await interaction.response.send_message("Ejecuta /configurar primero.", ephemeral=True)
    async for msg in channel.history(limit=30):
        if msg.author.id == bot.user.id and msg.content.startswith("**SOPORTE NEBULA**"):
            return await interaction.response.send_message("El panel ya existe.", ephemeral=True)
    await channel.send("**SOPORTE NEBULA**\nAbre un ticket privado para informar errores en la beta.", view=TicketPanel())
    await interaction.response.send_message("Panel publicado.", ephemeral=True)

@bot.tree.command(name="dar_beta", description="Dar acceso beta a un usuario", guild=GUILD)
@app_commands.checks.has_permissions(manage_roles=True)
async def dar_beta(interaction: discord.Interaction, usuario: discord.Member):
    role = discord.utils.get(interaction.guild.roles, name="Beta Tester")
    if not role:
        return await interaction.response.send_message("Ejecuta /configurar.", ephemeral=True)
    await usuario.add_roles(role)
    await interaction.response.send_message("Acceso concedido.", ephemeral=True)

@bot.tree.command(name="quitar_beta", description="Retirar acceso beta", guild=GUILD)
@app_commands.checks.has_permissions(manage_roles=True)
async def quitar_beta(interaction: discord.Interaction, usuario: discord.Member):
    role = discord.utils.get(interaction.guild.roles, name="Beta Tester")
    if role: await usuario.remove_roles(role)
    await interaction.response.send_message("Acceso retirado.", ephemeral=True)

@bot.tree.command(name="publicar_bienvenida", description="Publicar bienvenida y reglas", guild=GUILD)
@app_commands.checks.has_permissions(administrator=True)
async def publicar_bienvenida(interaction: discord.Interaction):
    guild = interaction.guild
    welcome = discord.utils.get(guild.text_channels, name="bienvenida")
    rules = discord.utils.get(guild.text_channels, name="reglas")
    if welcome: await welcome.send("**BIENVENIDO A NEBULA SYNTH**\nBeta del sintetizador para Mac y Windows. Usa #abrir-ticket para soporte y #avances-del-desarrollo para novedades.")
    if rules: await rules.send("**REGLAS**\n1. Respeto.\n2. No compartir claves privadas.\n3. No redistribuir betas.\n4. Reportar errores con sistema operativo, DAW y pasos.\n5. El rol Discord no controla la caducidad de una licencia VST.")
    await interaction.response.send_message("Mensajes publicados.", ephemeral=True)

@bot.event
async def on_ready():
    logging.info("NEBULA connected as %s in guild %s", bot.user, GUILD_ID)

async def main():
    async with bot:
        bot.add_view(TicketPanel())
        bot.add_view(CloseTicket())
        await bot.login(TOKEN)
        await bot.tree.sync(guild=GUILD)
        await bot.connect()

if __name__ == "__main__":
    import asyncio
    asyncio.run(main())
