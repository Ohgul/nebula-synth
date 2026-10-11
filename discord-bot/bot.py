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

@bot.tree.command(name="configurar", description="Crear canales y roles de NEBULA", guild=GUILD)
@app_commands.checks.has_permissions(administrator=True)
async def configurar(interaction: discord.Interaction):
    await interaction.response.defer(ephemeral=True)
    guild = interaction.guild
    if guild is None: return
    created = 0
    for name in ROLES:
        if discord.utils.get(guild.roles, name=name) is None:
            await guild.create_role(name=name)
            created += 1
    for name, channels in CATEGORIES.items():
        category = discord.utils.get(guild.categories, name=name)
        privacy = overwrites(guild, name)
        if category is None:
            category = await guild.create_category(name, overwrites=privacy)
            created += 1
        elif privacy is not None:
            await category.edit(overwrites=privacy)
        for channel_name in channels:
            if discord.utils.get(category.text_channels, name=channel_name) is None:
                await guild.create_text_channel(channel_name, category=category)
                created += 1
    await interaction.followup.send(f"Configuración lista: {created} elementos nuevos. Usa /publicar_panel.", ephemeral=True)

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
        await bot.tree.sync(guild=GUILD)
        await bot.start(TOKEN)

if __name__ == "__main__":
    import asyncio
    asyncio.run(main())
